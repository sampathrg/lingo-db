#include "mlir-support/parsing.h"
#include "mlir/Conversion/RelAlgToSubOp/RelAlgToSubOpPass.h"
#include "mlir/Dialect/Arithmetic/IR/Arithmetic.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/DB/IR/DBDialect.h"
#include "mlir/Dialect/DB/IR/DBOps.h"
#include "mlir/Dialect/DSA/IR/DSADialect.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/RelAlg/IR/RelAlgDialect.h"
#include "mlir/Dialect/RelAlg/IR/RelAlgOps.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/SCF/Transforms.h"
#include "mlir/Dialect/SubOperator/SubOperatorDialect.h"
#include "mlir/Dialect/SubOperator/SubOperatorOps.h"
#include "mlir/Dialect/TupleStream/TupleStreamOps.h"
#include "mlir/Dialect/util/UtilDialect.h"
#include "mlir/Dialect/util/UtilOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/Passes.h"
#include <mlir/Dialect/util/FunctionHelper.h>

using namespace mlir;

namespace {
struct RelalgToSubOpLoweringPass
   : public PassWrapper<RelalgToSubOpLoweringPass, OperationPass<ModuleOp>> {
   virtual llvm::StringRef getArgument() const override { return "to-subop"; }

   RelalgToSubOpLoweringPass() {}
   void getDependentDialects(DialectRegistry& registry) const override {
      registry.insert<LLVM::LLVMDialect, mlir::db::DBDialect, scf::SCFDialect, mlir::cf::ControlFlowDialect, util::UtilDialect, memref::MemRefDialect, arith::ArithmeticDialect, mlir::relalg::RelAlgDialect, mlir::subop::SubOperatorDialect>();
   }
   void runOnOperation() final;
};

class BaseTableLowering : public OpConversionPattern<mlir::relalg::BaseTableOp> {
   public:
   using OpConversionPattern<mlir::relalg::BaseTableOp>::OpConversionPattern;
   LogicalResult matchAndRewrite(mlir::relalg::BaseTableOp baseTableOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      std::vector<mlir::Type> types;
      std::vector<Attribute> colNames;
      std::vector<Attribute> colTypes;
      std::vector<NamedAttribute> mapping;
      std::string tableName = baseTableOp->getAttr("table_identifier").cast<mlir::StringAttr>().str();
      std::string scanDescription = R"({ "table": ")" + tableName + R"(", "columns": [ )";
      bool first = true;
      for (auto namedAttr : baseTableOp.columnsAttr().getValue()) {
         auto identifier = namedAttr.getName();
         auto attr = namedAttr.getValue();
         auto attrDef = attr.dyn_cast_or_null<mlir::tuples::ColumnDefAttr>();
         if (!first) {
            scanDescription += ",";
         } else {
            first = false;
         }
         scanDescription += "\"" + identifier.str() + "\"";
         colNames.push_back(rewriter.getStringAttr(identifier.strref()));
         colTypes.push_back(mlir::TypeAttr::get(attrDef.getColumn().type));
         mapping.push_back(rewriter.getNamedAttr(identifier.strref(), attrDef));
      }
      scanDescription += "] }";
      auto tableRefType = mlir::subop::TableRefType::get(rewriter.getContext(), mlir::subop::StateMembersAttr::get(rewriter.getContext(), rewriter.getArrayAttr(colNames), rewriter.getArrayAttr(colTypes)));
      mlir::Value tableRef = rewriter.create<mlir::subop::GetReferenceOp>(baseTableOp->getLoc(), tableRefType, rewriter.getStringAttr(scanDescription));
      rewriter.replaceOpWithNewOp<mlir::subop::ScanOp>(baseTableOp, tableRef, rewriter.getDictionaryAttr(mapping));
      return success();
   }
};
static mlir::LogicalResult safelyMoveRegion(ConversionPatternRewriter& rewriter, mlir::Region& source, mlir::Region& target) {
   rewriter.inlineRegionBefore(source, target, target.end());
   {
      if (!target.empty()) {
         source.push_back(new Block);
         std::vector<mlir::Location> locs;
         for (size_t i = 0; i < target.front().getArgumentTypes().size(); i++) {
            locs.push_back(target.front().getArgument(i).getLoc());
         }
         source.front().addArguments(target.front().getArgumentTypes(), locs);
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(&source.front());
         rewriter.create<mlir::tuples::ReturnOp>(rewriter.getUnknownLoc());
      }
   }
   return success();
}
static mlir::Value translateSelection(mlir::Value stream,mlir::Region& predicate, mlir::ConversionPatternRewriter& rewriter,mlir::Location loc){
   auto& columnManager = rewriter.getContext()->getLoadedDialect<mlir::tuples::TupleStreamDialect>()->getColumnManager();
   std::string scopeName = columnManager.getUniqueScope("map");
   std::string attributeName = "predicate";
   tuples::ColumnDefAttr markAttrDef = columnManager.createDef(scopeName, attributeName);
   auto& ra = markAttrDef.getColumn();
   ra.type = rewriter.getI1Type(); //todo: make sure it is really i1 (otherwise: nullable<i1> -> i1)
   auto mapOp = rewriter.create<mlir::subop::MapOp>(loc, mlir::tuples::TupleStreamType::get(rewriter.getContext()), stream, rewriter.getArrayAttr(markAttrDef));
   assert(safelyMoveRegion(rewriter, predicate, mapOp.fn()).succeeded());
   return rewriter.create<mlir::subop::FilterOp>(loc, mapOp.result(), rewriter.getArrayAttr(columnManager.createRef(&ra)));
}
class SelectionLowering : public OpConversionPattern<mlir::relalg::SelectionOp> {
   public:
   using OpConversionPattern<mlir::relalg::SelectionOp>::OpConversionPattern;

   LogicalResult matchAndRewrite(mlir::relalg::SelectionOp selectionOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      rewriter.replaceOp(selectionOp, translateSelection(adaptor.rel(),selectionOp.predicate(),rewriter,selectionOp->getLoc()));
      return success();
   }
};
class MapLowering : public OpConversionPattern<mlir::relalg::MapOp> {
   public:
   using OpConversionPattern<mlir::relalg::MapOp>::OpConversionPattern;

   LogicalResult matchAndRewrite(mlir::relalg::MapOp mapOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto mapOp2 = rewriter.replaceOpWithNewOp<mlir::subop::MapOp>(mapOp, mlir::tuples::TupleStreamType::get(rewriter.getContext()), adaptor.rel(), mapOp.computed_cols());
      assert(safelyMoveRegion(rewriter, mapOp.predicate(), mapOp2.fn()).succeeded());

      return success();
   }
};
class RenamingLowering : public OpConversionPattern<mlir::relalg::RenamingOp> {
   public:
   using OpConversionPattern<mlir::relalg::RenamingOp>::OpConversionPattern;

   LogicalResult matchAndRewrite(mlir::relalg::RenamingOp renamingOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto mapOp2 = rewriter.replaceOpWithNewOp<mlir::subop::RenamingOp>(renamingOp, mlir::tuples::TupleStreamType::get(rewriter.getContext()), adaptor.rel(), renamingOp.columns());
      return success();
   }
};
class ProjectionAllLowering : public OpConversionPattern<mlir::relalg::ProjectionOp> {
   public:
   using OpConversionPattern<mlir::relalg::ProjectionOp>::OpConversionPattern;

   LogicalResult matchAndRewrite(mlir::relalg::ProjectionOp projectionOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      if (projectionOp.set_semantic() == mlir::relalg::SetSemantic::distinct) return failure();
      rewriter.replaceOp(projectionOp, adaptor.rel());
      return success();
   }
};
class MaterializationHelper {
   std::vector<NamedAttribute> defMapping;
   std::vector<NamedAttribute> refMapping;
   std::vector<Attribute> types;
   std::vector<Attribute> names;
   std::unordered_map<const mlir::tuples::Column*, size_t> colToMemberPos;
   mlir::MLIRContext* context;

   public:
   MaterializationHelper(const mlir::relalg::ColumnSet& columns, mlir::MLIRContext* context) : context(context) {
      size_t i = 0;
      for (auto x : columns) {
         types.push_back(mlir::TypeAttr::get(x->type));
         colToMemberPos[x] = i;
         std::string name = "col" + std::to_string(i++);
         auto nameAttr = mlir::StringAttr::get(context, name);
         names.push_back(nameAttr);
         defMapping.push_back(mlir::NamedAttribute(nameAttr, context->getLoadedDialect<mlir::tuples::TupleStreamDialect>()->getColumnManager().createDef(x)));
         refMapping.push_back(mlir::NamedAttribute(nameAttr, context->getLoadedDialect<mlir::tuples::TupleStreamDialect>()->getColumnManager().createRef(x)));
      }
   }
   mlir::subop::StateMembersAttr createStateMembersAttr() {
      return mlir::subop::StateMembersAttr::get(context, mlir::ArrayAttr::get(context, names), mlir::ArrayAttr::get(context, types));
   }

   mlir::DictionaryAttr createStateColumnMapping() {
      return mlir::DictionaryAttr::get(context, defMapping);
   }
   mlir::DictionaryAttr createColumnstateMapping() {
      return mlir::DictionaryAttr::get(context, refMapping);
   }
   mlir::StringAttr lookupStateMemberForMaterializedColumn(const mlir::tuples::Column* column) {
      return names.at(colToMemberPos.at(column)).cast<mlir::StringAttr>();
   }
};
static mlir::Value translateNLJoin(mlir::Value left, mlir::Value right, mlir::relalg::ColumnSet columns, mlir::OpBuilder& rewriter, mlir::Location loc) {
   MaterializationHelper helper(columns, rewriter.getContext());
   auto vectorType = mlir::subop::VectorType::get(rewriter.getContext(), helper.createStateMembersAttr());
   mlir::Value vector;
   {
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToStart(rewriter.getInsertionBlock());
      vector = rewriter.create<mlir::subop::CreateOp>(loc, vectorType, "");
   }
   rewriter.create<mlir::subop::MaterializeOp>(loc, left, vector, helper.createColumnstateMapping());
   auto nestedMapOp = rewriter.create<mlir::subop::NestedMapOp>(loc, mlir::tuples::TupleStreamType::get(rewriter.getContext()), right, rewriter.getArrayAttr({}));
   auto b = new Block;
   nestedMapOp.region().push_back(b);
   {
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToStart(b);
      auto scan = rewriter.create<mlir::subop::ScanOp>(loc, vector, helper.createStateColumnMapping());
      rewriter.create<mlir::tuples::ReturnOp>(loc, scan.res());
   }
   return nestedMapOp.res();
}

class CrossProductLowering : public OpConversionPattern<mlir::relalg::CrossProductOp> {
   public:
   using OpConversionPattern<mlir::relalg::CrossProductOp>::OpConversionPattern;

   LogicalResult matchAndRewrite(mlir::relalg::CrossProductOp crossProductOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      rewriter.replaceOp(crossProductOp, translateNLJoin(adaptor.left(), adaptor.right(), mlir::cast<Operator>(crossProductOp.left().getDefiningOp()).getAvailableColumns(), rewriter, crossProductOp->getLoc()));
      return success();
   }
};
class InnerJoinNLLowering : public OpConversionPattern<mlir::relalg::InnerJoinOp> {
   public:
   using OpConversionPattern<mlir::relalg::InnerJoinOp>::OpConversionPattern;

   LogicalResult matchAndRewrite(mlir::relalg::InnerJoinOp innerJoinOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      auto cp=translateNLJoin(adaptor.left(), adaptor.right(), mlir::cast<Operator>(innerJoinOp.left().getDefiningOp()).getAvailableColumns(), rewriter, innerJoinOp->getLoc());
      rewriter.replaceOp(innerJoinOp, translateSelection(cp,innerJoinOp.predicate(),rewriter,innerJoinOp->getLoc()));
      return success();
   }
};
class SortLowering : public OpConversionPattern<mlir::relalg::SortOp> {
   public:
   using OpConversionPattern<mlir::relalg::SortOp>::OpConversionPattern;
   mlir::Value createSortPredicate(mlir::OpBuilder& builder, std::vector<std::pair<mlir::Value, mlir::Value>> sortCriteria, mlir::Value trueVal, mlir::Value falseVal, size_t pos, mlir::Location loc) const {
      if (pos < sortCriteria.size()) {
         mlir::Value lt = builder.create<mlir::db::CmpOp>(loc, mlir::db::DBCmpPredicate::lt, sortCriteria[pos].first, sortCriteria[pos].second);
         lt = builder.create<mlir::db::DeriveTruth>(loc, lt);
         auto ifOp = builder.create<mlir::scf::IfOp>(
            loc, builder.getI1Type(), lt, [&](mlir::OpBuilder& builder, mlir::Location loc) { builder.create<mlir::scf::YieldOp>(loc, trueVal); }, [&](mlir::OpBuilder& builder, mlir::Location loc) {
               mlir::Value eq = builder.create<mlir::db::CmpOp>(loc, mlir::db::DBCmpPredicate::eq, sortCriteria[pos].first, sortCriteria[pos].second);
               eq=builder.create<mlir::db::DeriveTruth>(loc,eq);
               auto ifOp2 = builder.create<mlir::scf::IfOp>(loc, builder.getI1Type(), eq,[&](mlir::OpBuilder& builder, mlir::Location loc) {
                     builder.create<mlir::scf::YieldOp>(loc, createSortPredicate(builder, sortCriteria, trueVal, falseVal, pos + 1,loc));
                  },[&](mlir::OpBuilder& builder, mlir::Location loc) {
                     builder.create<mlir::scf::YieldOp>(loc, falseVal);
                  });
               builder.create<mlir::scf::YieldOp>(loc, ifOp2.getResult(0)); });
         return ifOp.getResult(0);
      } else {
         return falseVal;
      }
   }
   LogicalResult matchAndRewrite(mlir::relalg::SortOp sortOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      MaterializationHelper helper(sortOp.getAvailableColumns(), rewriter.getContext());

      auto vectorType = mlir::subop::VectorType::get(rewriter.getContext(), helper.createStateMembersAttr());
      mlir::Value vector;
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(rewriter.getInsertionBlock());
         vector = rewriter.create<mlir::subop::CreateOp>(sortOp->getLoc(), vectorType, "");
      }
      rewriter.create<mlir::subop::MaterializeOp>(sortOp->getLoc(), adaptor.rel(), vector, helper.createColumnstateMapping());
      auto block = new Block;
      std::vector<Attribute> sortByMembers;
      std::vector<Type> argumentTypes;
      std::vector<Location> locs;
      for (auto attr : sortOp.sortspecs()) {
         auto sortspecAttr = attr.cast<mlir::relalg::SortSpecificationAttr>();
         argumentTypes.push_back(sortspecAttr.getAttr().getColumn().type);
         locs.push_back(sortOp->getLoc());
         sortByMembers.push_back(helper.lookupStateMemberForMaterializedColumn(&sortspecAttr.getAttr().getColumn()));
      }
      block->addArguments(argumentTypes, locs);
      block->addArguments(argumentTypes, locs);
      std::vector<std::pair<mlir::Value, mlir::Value>> sortCriteria;
      for (auto attr : sortOp.sortspecs()) {
         auto sortspecAttr = attr.cast<mlir::relalg::SortSpecificationAttr>();
         mlir::Value left = block->getArgument(sortCriteria.size());
         mlir::Value right = block->getArgument(sortCriteria.size() + sortOp.sortspecs().size());
         if (sortspecAttr.getSortSpec() == mlir::relalg::SortSpec::desc) {
            std::swap(left, right);
         }
         sortCriteria.push_back({left, right});
      }
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(block);
         auto trueVal = rewriter.create<mlir::db::ConstantOp>(sortOp->getLoc(), rewriter.getI1Type(), rewriter.getIntegerAttr(rewriter.getI1Type(), 1));
         auto falseVal = rewriter.create<mlir::db::ConstantOp>(sortOp->getLoc(), rewriter.getI1Type(), rewriter.getIntegerAttr(rewriter.getI1Type(), 0));
         rewriter.create<mlir::tuples::ReturnOp>(sortOp->getLoc(), createSortPredicate(rewriter, sortCriteria, trueVal, falseVal, 0, sortOp->getLoc()));
      }
      auto subOpSort = rewriter.create<mlir::subop::SortOp>(sortOp->getLoc(), vector, rewriter.getArrayAttr(sortByMembers));
      subOpSort.region().getBlocks().push_back(block);
      rewriter.replaceOpWithNewOp<mlir::subop::ScanOp>(sortOp, vector, helper.createStateColumnMapping());
      return success();
   }
};
class MaterializeLowering : public OpConversionPattern<mlir::relalg::MaterializeOp> {
   public:
   using OpConversionPattern<mlir::relalg::MaterializeOp>::OpConversionPattern;

   LogicalResult matchAndRewrite(mlir::relalg::MaterializeOp materializeOp, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      std::vector<Attribute> colNames;
      std::vector<Attribute> colTypes;
      std::vector<NamedAttribute> mapping;
      for (size_t i = 0; i < materializeOp.columns().size(); i++) {
         auto columnName = materializeOp.columns()[i].cast<mlir::StringAttr>().str();
         auto columnAttr = materializeOp.cols()[i].cast<mlir::tuples::ColumnRefAttr>();
         auto columnType = columnAttr.getColumn().type;
         colNames.push_back(rewriter.getStringAttr(columnName));
         colTypes.push_back(mlir::TypeAttr::get(columnType));
         mapping.push_back(rewriter.getNamedAttr(columnName, columnAttr));
      }
      auto tableRefType = mlir::subop::TableType::get(rewriter.getContext(), mlir::subop::StateMembersAttr::get(rewriter.getContext(), rewriter.getArrayAttr(colNames), rewriter.getArrayAttr(colTypes)));
      mlir::Value table;
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(rewriter.getInsertionBlock());
         table = rewriter.create<mlir::subop::CreateOp>(materializeOp->getLoc(), tableRefType, "");
      }
      rewriter.create<mlir::subop::MaterializeOp>(materializeOp->getLoc(), adaptor.rel(), table, rewriter.getDictionaryAttr(mapping));
      rewriter.replaceOpWithNewOp<mlir::subop::ConvertToExplicit>(materializeOp, mlir::dsa::TableType::get(rewriter.getContext()), table);

      return success();
   }
};

void RelalgToSubOpLoweringPass::runOnOperation() {
   auto module = getOperation();
   getContext().getLoadedDialect<mlir::util::UtilDialect>()->getFunctionHelper().setParentModule(module);

   // Define Conversion Target
   ConversionTarget target(getContext());
   target.addLegalOp<ModuleOp>();
   target.addLegalOp<UnrealizedConversionCastOp>();
   target.addIllegalDialect<relalg::RelAlgDialect>();
   target.addLegalDialect<subop::SubOperatorDialect>();
   target.addLegalDialect<db::DBDialect>();
   target.addLegalDialect<dsa::DSADialect>();

   target.addLegalDialect<tuples::TupleStreamDialect>();
   target.addLegalDialect<func::FuncDialect>();
   target.addLegalDialect<memref::MemRefDialect>();
   target.addLegalDialect<arith::ArithmeticDialect>();
   target.addLegalDialect<cf::ControlFlowDialect>();
   target.addLegalDialect<scf::SCFDialect>();
   target.addLegalDialect<util::UtilDialect>();

   TypeConverter typeConverter;
   typeConverter.addConversion([](mlir::tuples::TupleStreamType t) { return t; });
   auto* ctxt = &getContext();

   RewritePatternSet patterns(&getContext());

   patterns.insert<BaseTableLowering>(typeConverter, ctxt);
   patterns.insert<SelectionLowering>(typeConverter, ctxt);
   patterns.insert<MapLowering>(typeConverter, ctxt);
   patterns.insert<SortLowering>(typeConverter, ctxt);
   patterns.insert<MaterializeLowering>(typeConverter, ctxt);
   patterns.insert<RenamingLowering>(typeConverter, ctxt);
   patterns.insert<ProjectionAllLowering>(typeConverter, ctxt);
   patterns.insert<CrossProductLowering>(typeConverter, ctxt);
   patterns.insert<InnerJoinNLLowering>(typeConverter, ctxt);

   if (failed(applyFullConversion(module, target, std::move(patterns))))
      signalPassFailure();
}
} //namespace
std::unique_ptr<mlir::Pass>
mlir::relalg::createLowerToSubOpPass() {
   return std::make_unique<RelalgToSubOpLoweringPass>();
}
void mlir::relalg::createLowerRelAlgToSubOpPipeline(mlir::OpPassManager& pm) {
   pm.addPass(mlir::relalg::createLowerToSubOpPass());
}
void mlir::relalg::registerRelAlgToSubOpConversionPasses() {
   ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
      return mlir::relalg::createLowerToSubOpPass();
   });
   mlir::PassPipelineRegistration<EmptyPipelineOptions>(
      "lower-relalg-to-subop",
      "",
      mlir::relalg::createLowerRelAlgToSubOpPipeline);
}