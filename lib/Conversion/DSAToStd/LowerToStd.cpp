#include "mlir-support/parsing.h"
#include "mlir/Conversion/AffineToStandard/AffineToStandard.h"
#include "mlir/Conversion/DSAToStd/DSAToStd.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/UtilToLLVM/Passes.h"
#include "mlir/Dialect/DSA/IR/DSADialect.h"
#include "mlir/Dialect/DSA/IR/DSAOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/SCF/Transforms.h"
#include "mlir/Dialect/util/UtilDialect.h"
#include "mlir/Dialect/util/UtilOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include <mlir/IR/BuiltinTypes.h>

#include "runtime-defs/DataSourceIteration.h"
using namespace mlir;

namespace {

class ScanSourceLowering : public OpConversionPattern<mlir::dsa::ScanSource> {
   public:
   using OpConversionPattern<mlir::dsa::ScanSource>::OpConversionPattern;
   LogicalResult matchAndRewrite(mlir::dsa::ScanSource op, OpAdaptor adaptor, ConversionPatternRewriter& rewriter) const override {
      std::vector<Type> types;
      auto parentModule=op->getParentOfType<ModuleOp>();
      mlir::FuncOp funcOp=parentModule.lookupSymbol<mlir::FuncOp>("rt_get_execution_context");
      if(!funcOp){
         mlir::OpBuilder::InsertionGuard guard(rewriter);
         rewriter.setInsertionPointToStart(parentModule.getBody());
         funcOp = rewriter.create<FuncOp>(op->getLoc(), "rt_get_execution_context", rewriter.getFunctionType({},{mlir::util::RefType::get(getContext(),rewriter.getI8Type())}), rewriter.getStringAttr("private"));

      }

      mlir::Value executionContext =rewriter.create<mlir::func::CallOp>(op->getLoc(),funcOp,mlir::ValueRange{}).getResult(0);
      mlir::Value description = rewriter.create<mlir::util::CreateConstVarLen>(op->getLoc(), mlir::util::VarLen32Type::get(rewriter.getContext()), op.descrAttr());
      auto rawPtr = runtime::DataSourceIteration::start(rewriter, op->getLoc())({executionContext, description})[0];
      rewriter.replaceOp(op, rawPtr);
      return success();
   }
};
} // end anonymous namespace

namespace {
struct DSAToStdLoweringPass
   : public PassWrapper<DSAToStdLoweringPass, OperationPass<ModuleOp>> {
   virtual llvm::StringRef getArgument() const override { return "dsa-to-std"; }

   DSAToStdLoweringPass() {}
   void getDependentDialects(DialectRegistry& registry) const override {
      registry.insert<LLVM::LLVMDialect, mlir::dsa::DSADialect, scf::SCFDialect, mlir::cf::ControlFlowDialect, util::UtilDialect, memref::MemRefDialect, arith::ArithmeticDialect>();
   }
   void runOnOperation() final;
};
static TupleType convertTuple(TupleType tupleType, TypeConverter& typeConverter) {
   std::vector<Type> types;
   for (auto t : tupleType.getTypes()) {
      Type converted = typeConverter.convertType(t);
      converted = converted ? converted : t;
      types.push_back(converted);
   }
   return TupleType::get(tupleType.getContext(), TypeRange(types));
}
} // end anonymous namespace
static bool hasDSAType(TypeConverter& converter, TypeRange types) {
   return llvm::any_of(types, [&converter](mlir::Type t) { auto converted = converter.convertType(t);return converted&&converted!=t; });
}
template <class Op>
class SimpleTypeConversionPattern : public ConversionPattern {
   public:
   explicit SimpleTypeConversionPattern(TypeConverter& typeConverter, MLIRContext* context)
      : ConversionPattern(typeConverter, Op::getOperationName(), 1, context) {}

   LogicalResult
   matchAndRewrite(Operation* op, ArrayRef<Value> operands,
                   ConversionPatternRewriter& rewriter) const override {
      llvm::SmallVector<mlir::Type> convertedTypes;
      assert(typeConverter->convertTypes(op->getResultTypes(), convertedTypes).succeeded());
      rewriter.replaceOpWithNewOp<Op>(op, convertedTypes, ValueRange(operands), op->getAttrs());
      return success();
   }
};
void DSAToStdLoweringPass::runOnOperation() {
   auto module = getOperation();
   getContext().getLoadedDialect<mlir::util::UtilDialect>()->getFunctionHelper().setParentModule(module);
   // Define Conversion Target
   ConversionTarget target(getContext());
   target.addLegalOp<ModuleOp>();
   target.addLegalOp<UnrealizedConversionCastOp>();

   target.addLegalDialect<func::FuncDialect>();
   target.addLegalDialect<memref::MemRefDialect>();
   TypeConverter typeConverter;

   auto opIsWithoutDSATypes = [&](Operation* op) { return !hasDSAType(typeConverter, op->getOperandTypes()) && !hasDSAType(typeConverter, op->getResultTypes()); };
   target.addDynamicallyLegalDialect<scf::SCFDialect>(opIsWithoutDSATypes);
   target.addDynamicallyLegalDialect<arith::ArithmeticDialect>(opIsWithoutDSATypes);

   target.addLegalDialect<cf::ControlFlowDialect>();

   target.addDynamicallyLegalDialect<util::UtilDialect>(opIsWithoutDSATypes);
   target.addLegalOp<mlir::dsa::CondSkipOp>();

   target.addDynamicallyLegalOp<mlir::dsa::CondSkipOp>(opIsWithoutDSATypes);
   target.addDynamicallyLegalOp<FuncOp>([&](FuncOp op) {
      auto isLegal = !hasDSAType(typeConverter, op.getType().getInputs()) &&
         !hasDSAType(typeConverter, op.getType().getResults());
      return isLegal;
   });
   target.addDynamicallyLegalOp<func::ConstantOp>([&](func::ConstantOp op) {
      if (auto functionType = op.getType().dyn_cast_or_null<mlir::FunctionType>()) {
         auto isLegal = !hasDSAType(typeConverter, functionType.getInputs()) &&
            !hasDSAType(typeConverter, functionType.getResults());
         return isLegal;
      } else {
         return true;
      }
   });
   target.addDynamicallyLegalOp<func::CallOp, func::CallIndirectOp, func::ReturnOp>(opIsWithoutDSATypes);

   target.addDynamicallyLegalOp<util::SizeOfOp>(
      [&typeConverter](util::SizeOfOp op) {
         auto isLegal = !hasDSAType(typeConverter, op.type());
         return isLegal;
      });

   typeConverter.addConversion([&](mlir::TupleType tupleType) {
      return convertTuple(tupleType, typeConverter);
   });
   typeConverter.addConversion([&](mlir::dsa::TableType tableType) {
      return mlir::util::RefType::get(&getContext(), IntegerType::get(&getContext(), 8));
   });
   typeConverter.addConversion([&](mlir::dsa::TableBuilderType tableType) {
      return mlir::util::RefType::get(&getContext(), IntegerType::get(&getContext(), 8));
   });
   typeConverter.addConversion([&](mlir::IntegerType iType) { return iType; });
   typeConverter.addConversion([&](mlir::IndexType iType) { return iType; });
   typeConverter.addConversion([&](mlir::FloatType fType) { return fType; });
   typeConverter.addConversion([&](mlir::MemRefType refType) { return refType; });
   typeConverter.addSourceMaterialization([&](OpBuilder&, IntegerType type, ValueRange valueRange, Location loc) { return valueRange.front(); });
   typeConverter.addSourceMaterialization([&](OpBuilder&, dsa::TableType type, ValueRange valueRange, Location loc) { return valueRange.front(); });
   typeConverter.addSourceMaterialization([&](OpBuilder&, TupleType type, ValueRange valueRange, Location loc) { return valueRange.front(); });

   RewritePatternSet patterns(&getContext());

   mlir::populateFunctionOpInterfaceTypeConversionPattern<mlir::FuncOp>(patterns, typeConverter);
   mlir::populateCallOpTypeConversionPattern(patterns, typeConverter);
   mlir::populateReturnOpTypeConversionPattern(patterns, typeConverter);
   mlir::dsa::populateScalarToStdPatterns(typeConverter, patterns);
   mlir::dsa::populateDSAToStdPatterns(typeConverter, patterns);
   mlir::dsa::populateCollectionsToStdPatterns(typeConverter, patterns);
   mlir::util::populateUtilTypeConversionPatterns(typeConverter, patterns);
   mlir::scf::populateSCFStructuralTypeConversionsAndLegality(typeConverter, patterns, target);
   patterns.insert<SimpleTypeConversionPattern<mlir::func::ConstantOp>>(typeConverter, &getContext());
   patterns.insert<SimpleTypeConversionPattern<mlir::arith::SelectOp>>(typeConverter, &getContext());
   patterns.insert<SimpleTypeConversionPattern<mlir::dsa::CondSkipOp>>(typeConverter, &getContext());
   patterns.insert<ScanSourceLowering>(typeConverter, &getContext());

   if (failed(applyFullConversion(module, target, std::move(patterns))))
      signalPassFailure();
}

std::unique_ptr<mlir::Pass> mlir::dsa::createLowerToStdPass() {
   return std::make_unique<DSAToStdLoweringPass>();
}
