#include "llvm/ADT/TypeSwitch.h"

#include "mlir/Dialect/DB/IR/DBDialect.h"
#include "mlir/Dialect/DB/IR/DBOps.h"
#include "mlir/Dialect/SubOperator/SubOperatorDialect.h"
#include "mlir/Dialect/SubOperator/SubOperatorOps.h"
#include "mlir/Dialect/SubOperator/Transforms/ColumnUsageAnalysis.h"
#include "mlir/Dialect/SubOperator/Transforms/Passes.h"
#include "mlir/Dialect/SubOperator/Transforms/StateUsageTransformer.h"
#include "mlir/Dialect/SubOperator/Transforms/SubOpDependencyAnalysis.h"
#include "mlir/Dialect/TupleStream/TupleStreamDialect.h"
#include "mlir/Dialect/TupleStream/TupleStreamOps.h"
#include "mlir/Dialect/util/UtilDialect.h"
#include "mlir/Dialect/util/UtilOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
namespace {
static std::pair<mlir::tuples::ColumnDefAttr, mlir::tuples::ColumnRefAttr> createColumn(mlir::Type type, std::string scope, std::string name) {
   auto& columnManager = type.getContext()->getLoadedDialect<mlir::tuples::TupleStreamDialect>()->getColumnManager();
   std::string scopeName = columnManager.getUniqueScope(scope);
   std::string attributeName = name;
   mlir::tuples::ColumnDefAttr markAttrDef = columnManager.createDef(scopeName, attributeName);
   auto& ra = markAttrDef.getColumn();
   ra.type = type;
   return {markAttrDef, columnManager.createRef(&ra)};
}

class MultiMapAsHashIndexedView : public mlir::RewritePattern {
   const mlir::subop::ColumnUsageAnalysis& analysis;

   public:
   MultiMapAsHashIndexedView(mlir::MLIRContext* context, mlir::subop::ColumnUsageAnalysis& analysis)
      : RewritePattern(mlir::subop::GenericCreateOp::getOperationName(), 1, context), analysis(analysis) {}
   mlir::LogicalResult matchAndRewrite(mlir::Operation* op, mlir::PatternRewriter& rewriter) const override {
      auto& memberManager = getContext()->getLoadedDialect<mlir::subop::SubOperatorDialect>()->getMemberManager();
      auto& columnManager = getContext()->getLoadedDialect<mlir::tuples::TupleStreamDialect>()->getColumnManager();

      auto createOp = mlir::cast<mlir::subop::GenericCreateOp>(op);
      auto state = createOp.getRes();
      auto multiMapType = state.getType().dyn_cast_or_null<mlir::subop::MultiMapType>();
      if (!multiMapType) {
         return mlir::failure();
      }
      std::vector<mlir::subop::LookupOp> lookupOps;
      std::vector<mlir::Operation*> otherUses;
      mlir::subop::InsertOp insertOp;
      for (auto* u : state.getUsers()) {
         if (auto mOp = mlir::dyn_cast_or_null<mlir::subop::InsertOp>(u)) {
            if (insertOp) {
               return mlir::failure();
            } else {
               insertOp = mOp;
            }
         } else if (auto lookupOp = mlir::dyn_cast_or_null<mlir::subop::LookupOp>(u)) {
            lookupOps.push_back(lookupOp);
         } else {
            otherUses.push_back(u);
         }
      }

      auto hashMember = memberManager.getUniqueMember("hash");
      auto linkMember = memberManager.getUniqueMember("link");
      auto [hashDef, hashRef] = createColumn(rewriter.getIndexType(), "hj", "hash");
      auto linkType = mlir::util::RefType::get(rewriter.getContext(), rewriter.getI8Type());
      auto [linkDef, linkRef] = createColumn(linkType, "hj", "link");
      auto loc = op->getLoc();

      std::vector<mlir::Attribute> bufferMemberTypes{mlir::TypeAttr::get(linkType), mlir::TypeAttr::get(rewriter.getIndexType())};
      std::vector<mlir::Attribute> bufferMemberNames{rewriter.getStringAttr(linkMember), rewriter.getStringAttr(hashMember)};
      bufferMemberNames.insert(bufferMemberNames.end(), multiMapType.getMembers().getNames().begin(), multiMapType.getMembers().getNames().end());
      bufferMemberTypes.insert(bufferMemberTypes.end(), multiMapType.getMembers().getTypes().begin(), multiMapType.getMembers().getTypes().end());
      std::vector<mlir::Attribute> hashIndexedViewTypes{multiMapType.getMembers().getTypes().begin(), multiMapType.getMembers().getTypes().end()};
      std::vector<mlir::Attribute> hashIndexedViewNames{multiMapType.getMembers().getNames().begin(), multiMapType.getMembers().getNames().end()};

      auto bufferType = mlir::subop::BufferType::get(rewriter.getContext(), mlir::subop::StateMembersAttr::get(getContext(), rewriter.getArrayAttr(bufferMemberNames), rewriter.getArrayAttr(bufferMemberTypes)));
      mlir::Value buffer;
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPoint(createOp);
         buffer = rewriter.create<mlir::subop::GenericCreateOp>(loc, bufferType);
      }
      mlir::Type hashIndexedViewType;
      mlir::Value hashIndexedView;
      {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPoint(insertOp);
         auto mapOp = rewriter.create<mlir::subop::MapOp>(loc, insertOp.getStream(), rewriter.getArrayAttr({hashDef, linkDef}));
         std::vector<mlir::NamedAttribute> newMapping(insertOp.getMapping().begin(), insertOp.getMapping().end());
         newMapping.push_back(rewriter.getNamedAttr(hashMember, hashRef));
         newMapping.push_back(rewriter.getNamedAttr(linkMember, linkRef));
         rewriter.create<mlir::subop::MaterializeOp>(loc, mapOp.getResult(), buffer, rewriter.getDictionaryAttr(newMapping));
         hashIndexedViewType = mlir::subop::HashIndexedViewType::get(rewriter.getContext(), mlir::subop::StateMembersAttr::get(rewriter.getContext(), rewriter.getArrayAttr({rewriter.getStringAttr(hashMember)}), rewriter.getArrayAttr({mlir::TypeAttr::get(rewriter.getIndexType())})), mlir::subop::StateMembersAttr::get(getContext(), rewriter.getArrayAttr(hashIndexedViewNames), rewriter.getArrayAttr(hashIndexedViewTypes)));
         hashIndexedView = rewriter.create<mlir::subop::CreateHashIndexedView>(loc, hashIndexedViewType, buffer, hashMember, linkMember);
         auto* mapBlock = new mlir::Block;
         mlir::Value tuple = mapBlock->addArgument(mlir::tuples::TupleType::get(getContext()), loc);
         mapOp.getFn().push_back(mapBlock);
         rewriter.setInsertionPointToStart(mapBlock);
         std::vector<mlir::Value> values;
         for (auto keyMemberName : multiMapType.getKeyMembers().getNames()) {
            auto keyColumn = insertOp.getMapping().get(keyMemberName.cast<mlir::StringAttr>().strref()).cast<mlir::tuples::ColumnRefAttr>();
            values.push_back(rewriter.create<mlir::tuples::GetColumnOp>(loc, keyColumn.getColumn().type, keyColumn, tuple));
         }
         mlir::Value hashed = rewriter.create<mlir::db::Hash>(loc, rewriter.create<mlir::util::PackOp>(loc, values));
         mlir::Value inValidLink = rewriter.create<mlir::util::InvalidRefOp>(loc, linkType);
         rewriter.create<mlir::tuples::ReturnOp>(loc, mlir::ValueRange{hashed, inValidLink});
      }
      auto entryRefType = mlir::subop::LookupEntryRefType::get(rewriter.getContext(), hashIndexedViewType.cast<mlir::subop::LookupAbleState>());
      auto entryRefListType = mlir::subop::ListType::get(rewriter.getContext(), entryRefType);
      mlir::subop::SubOpStateUsageTransformer transformer(analysis, getContext(), [&](mlir::Operation* op, mlir::Type type) -> mlir::Type {
         return llvm::TypeSwitch<mlir::Operation*, mlir::Type>(op)
            .Case([&](mlir::subop::ScanListOp scanListOp) {
               return entryRefType;
            })
            .Default([&](mlir::Operation* op) {
               assert(false && "not supported yet");
               return mlir::Type();
            });

         //
      });
      for (auto lookupOp : lookupOps) {
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPoint(lookupOp);
         auto [hashDefLookup, hashRefLookup] = createColumn(rewriter.getIndexType(), "hj", "hash");
         auto lookupPred = createColumn(rewriter.getI1Type(), "lookup", "pred");
         auto lookupPredDef = lookupPred.first;
         auto lookupPredRef = lookupPred.second;
         auto mapOp = rewriter.create<mlir::subop::MapOp>(loc, lookupOp.getStream(), rewriter.getArrayAttr({hashDefLookup}));
         auto [listDef, listRef] = createColumn(entryRefListType, "lookup", "list");
         auto lookupRef = lookupOp.getRef();
         auto lookupKeys = lookupOp.getKeys();
         std::vector<mlir::NamedAttribute> gatheredForEqFn;
         std::vector<mlir::tuples::ColumnRefAttr> keyRefsForEqFn;
         for (auto keyMember : llvm::zip(multiMapType.getKeyMembers().getNames(), multiMapType.getKeyMembers().getTypes())) {
            auto name = std::get<0>(keyMember).cast<mlir::StringAttr>().str();
            auto type = std::get<1>(keyMember).cast<mlir::TypeAttr>().getValue();
            auto [lookupKeyMemberDef, lookupKeyMemberRef] = createColumn(type, "lookup", name);
            gatheredForEqFn.push_back(rewriter.getNamedAttr(name, lookupKeyMemberDef));
            keyRefsForEqFn.push_back(lookupKeyMemberRef);
         }
         auto* predFnBlock = new mlir::Block;
         {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(predFnBlock);
            mlir::Value tuple = predFnBlock->addArgument(mlir::tuples::TupleType::get(getContext()), loc);
            mlir::IRMapping mapping;
            size_t i = 0;
            for (auto key : keyRefsForEqFn) {
               mapping.map(lookupOp.getEqFn().getArgument(i++), rewriter.create<mlir::tuples::GetColumnOp>(loc, key.getColumn().type, key, tuple));
            }
            for (auto key : lookupKeys) {
               auto keyColumn = key.cast<mlir::tuples::ColumnRefAttr>();
               mapping.map(lookupOp.getEqFn().getArgument(i++), rewriter.create<mlir::tuples::GetColumnOp>(loc, keyColumn.getColumn().type, keyColumn, tuple));
            }
            for (auto& op : lookupOp.getEqFn().front()) {
               rewriter.clone(op, mapping);
            }
         }
         rewriter.replaceOpWithNewOp<mlir::subop::LookupOp>(lookupOp, mlir::tuples::TupleStreamType::get(rewriter.getContext()), mapOp.getResult(), hashIndexedView, rewriter.getArrayAttr({hashRefLookup}), listDef);
         auto* mapBlock = new mlir::Block;
         mlir::Value tuple = mapBlock->addArgument(mlir::tuples::TupleType::get(getContext()), loc);
         mapOp.getFn().push_back(mapBlock);
         rewriter.setInsertionPointToStart(mapBlock);
         std::vector<mlir::Value> values;
         for (auto key : lookupKeys) {
            auto keyColumn = key.cast<mlir::tuples::ColumnRefAttr>();
            values.push_back(rewriter.create<mlir::tuples::GetColumnOp>(loc, keyColumn.getColumn().type, keyColumn, tuple));
         }
         mlir::Value hashed = rewriter.create<mlir::db::Hash>(loc, rewriter.create<mlir::util::PackOp>(loc, values));
         rewriter.create<mlir::tuples::ReturnOp>(loc, mlir::ValueRange{hashed});
         mlir::Value currentTuple;
         transformer.setCallBeforeFn([&](mlir::Operation* op) {
            if (auto nestedMapOp = mlir::dyn_cast_or_null<mlir::subop::NestedMapOp>(op)) {
               currentTuple = nestedMapOp.getRegion().getArgument(0);
            }
         });
         transformer.setCallAfterFn([&](mlir::Operation* op) {
            if (auto scanListOp = mlir::dyn_cast_or_null<mlir::subop::ScanListOp>(op)) {
               assert(!!currentTuple);
               rewriter.setInsertionPointAfter(scanListOp);
               auto combined = rewriter.create<mlir::subop::CombineTupleOp>(loc, scanListOp.getRes(), currentTuple);
               auto gatherOp = rewriter.create<mlir::subop::GatherOp>(loc, combined.getRes(), columnManager.createRef(&scanListOp.getElem().getColumn()), rewriter.getDictionaryAttr(gatheredForEqFn));
               auto mapOp = rewriter.create<mlir::subop::MapOp>(loc, gatherOp.getRes(), rewriter.getArrayAttr({lookupPredDef}));
               mapOp.getFn().push_back(predFnBlock);
               auto filter = rewriter.create<mlir::subop::FilterOp>(loc, mapOp.getResult(), mlir::subop::FilterSemantic::all_true, rewriter.getArrayAttr({lookupPredRef}));
               scanListOp.getRes().replaceAllUsesExcept(filter.getResult(), combined);
            }
         });
         transformer.replaceColumn(&lookupRef.getColumn(), &listDef.getColumn());
         transformer.setCallBeforeFn({});
         transformer.setCallAfterFn({});
      }
      rewriter.eraseOp(insertOp);
      transformer.updateValue(state, buffer.getType());
      rewriter.replaceOp(createOp, buffer);
      return mlir::success();
   }
};
class MapAsHashMap : public mlir::RewritePattern {
   const mlir::subop::ColumnUsageAnalysis& analysis;

   public:
   MapAsHashMap(mlir::MLIRContext* context, mlir::subop::ColumnUsageAnalysis& analysis)
      : RewritePattern(mlir::subop::GenericCreateOp::getOperationName(), 1, context), analysis(analysis) {}
   mlir::LogicalResult matchAndRewrite(mlir::Operation* op, mlir::PatternRewriter& rewriter) const override {
      auto createOp = mlir::cast<mlir::subop::GenericCreateOp>(op);
      auto mapType = createOp.getType().dyn_cast_or_null<mlir::subop::MapType>();
      if (!mapType) {
         return mlir::failure();
      }
      auto hashMapType = mlir::subop::HashMapType::get(getContext(), mapType.getKeyMembers(), mapType.getValueMembers());

      mlir::TypeConverter typeConverter;
      typeConverter.addConversion([&](mlir::subop::ListType listType) {
         return mlir::subop::ListType::get(listType.getContext(), typeConverter.convertType(listType.getT()).cast<mlir::subop::StateEntryReference>());
      });
      typeConverter.addConversion([&](mlir::subop::OptionalType optionalType) {
         return mlir::subop::OptionalType::get(optionalType.getContext(), typeConverter.convertType(optionalType.getT()).cast<mlir::subop::StateEntryReference>());
      });
      typeConverter.addConversion([&](mlir::subop::MapEntryRefType refType) {
         return mlir::subop::HashMapEntryRefType::get(refType.getContext(), hashMapType);
      });
      typeConverter.addConversion([&](mlir::subop::LookupEntryRefType lookupRefType) {
         return mlir::subop::LookupEntryRefType::get(lookupRefType.getContext(), typeConverter.convertType(lookupRefType.getState()).cast<mlir::subop::LookupAbleState>());
      });
      typeConverter.addConversion([&](mlir::subop::MapType mapType) {
         return hashMapType;
      });
      mlir::subop::SubOpStateUsageTransformer transformer(analysis, getContext(), [&](mlir::Operation* op, mlir::Type type) -> mlir::Type {
         return typeConverter.convertType(type);
      });
      transformer.updateValue(createOp.getRes(), hashMapType);
      rewriter.replaceOpWithNewOp<mlir::subop::GenericCreateOp>(op, hashMapType);

      return mlir::success();
   }
};
class MultiMapAsHashMultiMap : public mlir::RewritePattern {
   const mlir::subop::ColumnUsageAnalysis& analysis;

   public:
   MultiMapAsHashMultiMap(mlir::MLIRContext* context, mlir::subop::ColumnUsageAnalysis& analysis)
      : RewritePattern(mlir::subop::GenericCreateOp::getOperationName(), 1, context), analysis(analysis) {}
   mlir::LogicalResult matchAndRewrite(mlir::Operation* op, mlir::PatternRewriter& rewriter) const override {
      auto createOp = mlir::cast<mlir::subop::GenericCreateOp>(op);
      auto multiMapType = createOp.getType().dyn_cast_or_null<mlir::subop::MultiMapType>();
      if (!multiMapType) {
         return mlir::failure();
      }
      auto hashMapType = mlir::subop::HashMultiMapType::get(getContext(), multiMapType.getKeyMembers(), multiMapType.getValueMembers());

      mlir::TypeConverter typeConverter;
      typeConverter.addConversion([&](mlir::subop::ListType listType) {
         return mlir::subop::ListType::get(listType.getContext(), typeConverter.convertType(listType.getT()).cast<mlir::subop::StateEntryReference>());
      });
      typeConverter.addConversion([&](mlir::subop::OptionalType optionalType) {
         return mlir::subop::OptionalType::get(optionalType.getContext(), typeConverter.convertType(optionalType.getT()).cast<mlir::subop::StateEntryReference>());
      });
      typeConverter.addConversion([&](mlir::subop::MultiMapEntryRefType refType) {
         return mlir::subop::HashMultiMapEntryRefType::get(refType.getContext(), hashMapType);
      });
      typeConverter.addConversion([&](mlir::subop::LookupEntryRefType lookupRefType) {
         return mlir::subop::LookupEntryRefType::get(lookupRefType.getContext(), typeConverter.convertType(lookupRefType.getState()).cast<mlir::subop::LookupAbleState>());
      });
      typeConverter.addConversion([&](mlir::subop::MultiMapType mapType) {
         return hashMapType;
      });
      mlir::subop::SubOpStateUsageTransformer transformer(analysis, getContext(), [&](mlir::Operation* op, mlir::Type type) -> mlir::Type {
         return typeConverter.convertType(type);
      });
      transformer.updateValue(createOp.getRes(), hashMapType);
      rewriter.replaceOpWithNewOp<mlir::subop::GenericCreateOp>(op, hashMapType);

      return mlir::success();
   }
};
class SpecializeSubOpPass : public mlir::PassWrapper<SpecializeSubOpPass, mlir::OperationPass<mlir::ModuleOp>> {
   bool withOptimizations;

   public:
   MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SpecializeSubOpPass)
   virtual llvm::StringRef getArgument() const override { return "subop-specialize"; }

   SpecializeSubOpPass(bool withOptimizations) : withOptimizations(withOptimizations) {}
   void getDependentDialects(mlir::DialectRegistry& registry) const override {
      registry.insert<mlir::util::UtilDialect, mlir::db::DBDialect>();
   }
   void runOnOperation() override {
      //transform "standalone" aggregation functions
      auto columnUsageAnalysis = getAnalysis<mlir::subop::ColumnUsageAnalysis>();

      mlir::RewritePatternSet patterns(&getContext());
      if (withOptimizations) {
         patterns.insert<MultiMapAsHashIndexedView>(&getContext(), columnUsageAnalysis);
      }
      patterns.insert<MapAsHashMap>(&getContext(), columnUsageAnalysis);
      patterns.insert<MultiMapAsHashMultiMap>(&getContext(), columnUsageAnalysis);

      if (mlir::applyPatternsAndFoldGreedily(getOperation().getRegion(), std::move(patterns)).failed()) {
         assert(false && "should not happen");
      }
   }
};
} // end anonymous namespace

std::unique_ptr<mlir::Pass>
mlir::subop::createSpecializeSubOpPass(bool withOptimizations) { return std::make_unique<SpecializeSubOpPass>(withOptimizations); }