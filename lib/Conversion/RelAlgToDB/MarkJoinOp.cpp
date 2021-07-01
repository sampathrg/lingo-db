#include "mlir/Conversion/RelAlgToDB/ProducerConsumerNode.h"
#include "mlir/Dialect/DB/IR/DBOps.h"
#include "mlir/Dialect/RelAlg/IR/RelAlgOps.h"
#include "mlir/Dialect/util/UtilOps.h"
#include <mlir/Conversion/RelAlgToDB/HashJoinUtils.h>
#include <mlir/IR/BlockAndValueMapping.h>

class NLMarkJoinLowering : public mlir::relalg::ProducerConsumerNode {
   mlir::relalg::MarkJoinOp joinOp;
   mlir::Value matchFoundFlag;

   public:
   NLMarkJoinLowering(mlir::relalg::MarkJoinOp markJoinOp) : mlir::relalg::ProducerConsumerNode({markJoinOp.left(), markJoinOp.right()}), joinOp(markJoinOp) {
   }
   virtual void setInfo(mlir::relalg::ProducerConsumerNode* consumer, mlir::relalg::Attributes requiredAttributes) override {
      this->consumer = consumer;
      this->requiredAttributes = requiredAttributes;
      this->requiredAttributes.insert(joinOp.getUsedAttributes());
      propagateInfo();
   }
   virtual mlir::relalg::Attributes getAvailableAttributes() override {
      return this->children[0]->getAvailableAttributes();
   }
   virtual void consume(mlir::relalg::ProducerConsumerNode* child, mlir::relalg::ProducerConsumerBuilder& builder, mlir::relalg::LoweringContext& context) override {
      auto scope = context.createScope();
      if (child == this->children[0].get()) {
         matchFoundFlag = builder.create<mlir::db::CreateFlag>(joinOp->getLoc(), mlir::db::FlagType::get(builder.getContext()));
         children[1]->setFlag(matchFoundFlag);
         children[1]->produce(context, builder);
         mlir::Value matchFound = builder.create<mlir::db::GetFlag>(joinOp->getLoc(), mlir::db::BoolType::get(builder.getContext()), matchFoundFlag);
         context.setValueForAttribute(scope,&joinOp.markattr().getRelationalAttribute(),matchFound);
         consumer->consume(this, builder, context);
      } else if (child == this->children[1].get()) {
         mlir::relalg::MarkJoinOp clonedMarkJoinOp = mlir::dyn_cast<mlir::relalg::MarkJoinOp>(joinOp->clone());
         mlir::Block* block = &clonedMarkJoinOp.predicate().getBlocks().front();
         auto* terminator = block->getTerminator();

         builder.mergeRelatinalBlock(block, context, scope);
         mlir::Value matched = mlir::cast<mlir::relalg::ReturnOp>(terminator).results()[0];
         builder.create<mlir::db::SetFlag>(joinOp->getLoc(), matchFoundFlag, matched);
         terminator->erase();
         clonedMarkJoinOp->destroy();
      }
   }
   virtual void produce(mlir::relalg::LoweringContext& context, mlir::relalg::ProducerConsumerBuilder& builder) override {
      children[0]->produce(context, builder);
   }

   virtual ~NLMarkJoinLowering() {}
};
class HashMarkJoinLowering : public mlir::relalg::HJNode<mlir::relalg::MarkJoinOp> {
   mlir::Value matchFoundFlag;
   public:
   HashMarkJoinLowering(mlir::relalg::MarkJoinOp innerJoinOp) : mlir::relalg::HJNode<mlir::relalg::MarkJoinOp>(innerJoinOp,innerJoinOp.right(), innerJoinOp.left()) {
   }

   virtual void handleLookup(mlir::Value matched, mlir::relalg::LoweringContext& context, mlir::relalg::ProducerConsumerBuilder& builder) {
      builder.create<mlir::db::SetFlag>(joinOp->getLoc(), matchFoundFlag, matched);
   }
   mlir::Value getFlag() override{
      return matchFoundFlag;
   }
   void beforeLookup(mlir::relalg::LoweringContext &context, mlir::relalg::ProducerConsumerBuilder &builder) override{
      matchFoundFlag = builder.create<mlir::db::CreateFlag>(joinOp->getLoc(), mlir::db::FlagType::get(builder.getContext()));
   }
   void afterLookup(mlir::relalg::LoweringContext &context, mlir::relalg::ProducerConsumerBuilder &builder) override {
      auto scope = context.createScope();
      mlir::Value matchFound = builder.create<mlir::db::GetFlag>(joinOp->getLoc(), mlir::db::BoolType::get(builder.getContext()), matchFoundFlag);
      context.setValueForAttribute(scope,&joinOp.markattr().getRelationalAttribute(),matchFound);
      consumer->consume(this, builder, context);
   }
   virtual ~HashMarkJoinLowering() {}
};
bool mlir::relalg::ProducerConsumerNodeRegistry::registeredMarkJoinOp = mlir::relalg::ProducerConsumerNodeRegistry::registerNode([](mlir::relalg::MarkJoinOp joinOp) {
   if (joinOp->hasAttr("impl")) {
      if (auto impl = joinOp->getAttr("impl").dyn_cast_or_null<mlir::StringAttr>()) {
         if (impl.getValue() == "hash") {
            return (std::unique_ptr<mlir::relalg::ProducerConsumerNode>) std::make_unique<HashMarkJoinLowering>(joinOp);
         }
      }
   }
   return (std::unique_ptr<mlir::relalg::ProducerConsumerNode>) std::make_unique<NLMarkJoinLowering>(joinOp);
});