#include "mlir/Dialect/SubOperator/SubOperatorOps.h"
#include "mlir/Dialect/TupleStream/ColumnManager.h"
#include "mlir/Dialect/TupleStream/TupleStreamDialect.h"
#include "mlir/Dialect/TupleStream/TupleStreamOpsAttributes.h"

#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpImplementation.h"

#include <llvm/ADT/TypeSwitch.h>

#include <iostream>
#include <queue>

using namespace mlir;
static mlir::tuples::ColumnManager& getColumnManager(::mlir::OpAsmParser& parser) {
   return parser.getBuilder().getContext()->getLoadedDialect<mlir::tuples::TupleStreamDialect>()->getColumnManager();
}
static ParseResult parseCustRef(OpAsmParser& parser, mlir::tuples::ColumnRefAttr& attr) {
   ::mlir::SymbolRefAttr parsedSymbolRefAttr;
   if (parser.parseAttribute(parsedSymbolRefAttr, parser.getBuilder().getType<::mlir::NoneType>())) { return failure(); }
   attr = getColumnManager(parser).createRef(parsedSymbolRefAttr);
   return success();
}

static void printCustRef(OpAsmPrinter& p, mlir::Operation* op, mlir::tuples::ColumnRefAttr attr) {
   p << attr.getName();
}
static ParseResult parseCustRefArr(OpAsmParser& parser, ArrayAttr& attr) {
   ArrayAttr parsedAttr;
   std::vector<Attribute> attributes;
   if (parser.parseAttribute(parsedAttr, parser.getBuilder().getType<::mlir::NoneType>())) {
      return failure();
   }
   for (auto a : parsedAttr) {
      SymbolRefAttr parsedSymbolRefAttr = a.dyn_cast<SymbolRefAttr>();
      mlir::tuples::ColumnRefAttr attr = getColumnManager(parser).createRef(parsedSymbolRefAttr);
      attributes.push_back(attr);
   }
   attr = ArrayAttr::get(parser.getBuilder().getContext(), attributes);
   return success();
}

static void printCustRefArr(OpAsmPrinter& p, mlir::Operation* op, ArrayAttr arrayAttr) {
   p << "[";
   std::vector<Attribute> attributes;
   bool first = true;
   for (auto a : arrayAttr) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      mlir::tuples::ColumnRefAttr parsedSymbolRefAttr = a.dyn_cast<mlir::tuples::ColumnRefAttr>();
      p << parsedSymbolRefAttr.getName();
   }
   p << "]";
}
static ParseResult parseCustDef(OpAsmParser& parser, mlir::tuples::ColumnDefAttr& attr) {
   SymbolRefAttr attrSymbolAttr;
   if (parser.parseAttribute(attrSymbolAttr, parser.getBuilder().getType<::mlir::NoneType>())) { return failure(); }
   std::string attrName(attrSymbolAttr.getLeafReference().getValue());
   if (parser.parseLParen()) { return failure(); }
   DictionaryAttr dictAttr;
   if (parser.parseAttribute(dictAttr)) { return failure(); }
   mlir::ArrayAttr fromExisting;
   if (parser.parseRParen()) { return failure(); }
   if (parser.parseOptionalEqual().succeeded()) {
      if (parseCustRefArr(parser, fromExisting)) {
         return failure();
      }
   }
   attr = getColumnManager(parser).createDef(attrSymbolAttr, fromExisting);
   auto propType = dictAttr.get("type").dyn_cast<TypeAttr>().getValue();
   attr.getColumn().type = propType;
   return success();
}
static void printCustDef(OpAsmPrinter& p, mlir::Operation* op, mlir::tuples::ColumnDefAttr attr) {
   p << attr.getName();
   std::vector<mlir::NamedAttribute> relAttrDefProps;
   MLIRContext* context = attr.getContext();
   const mlir::tuples::Column& relationalAttribute = attr.getColumn();
   relAttrDefProps.push_back({mlir::StringAttr::get(context, "type"), mlir::TypeAttr::get(relationalAttribute.type)});
   p << "(" << mlir::DictionaryAttr::get(context, relAttrDefProps) << ")";
   Attribute fromExisting = attr.getFromExisting();
   if (fromExisting) {
      ArrayAttr fromExistingArr = fromExisting.dyn_cast_or_null<ArrayAttr>();
      p << "=";
      printCustRefArr(p, op, fromExistingArr);
   }
}

static ParseResult parseStateColumnMapping(OpAsmParser& parser, DictionaryAttr& attr) {
   if (parser.parseLBrace()) return failure();
   std::vector<mlir::NamedAttribute> columns;
   while (true) {
      if (!parser.parseOptionalRBrace()) { break; }
      StringRef colName;
      if (parser.parseKeyword(&colName)) { return failure(); }
      if (parser.parseEqual() || parser.parseGreater()) { return failure(); }
      mlir::tuples::ColumnDefAttr attrDefAttr;
      if (parseCustDef(parser, attrDefAttr)) {
         return failure();
      }
      columns.push_back({StringAttr::get(parser.getBuilder().getContext(), colName), attrDefAttr});
      if (!parser.parseOptionalComma()) { continue; }
      if (parser.parseRBrace()) { return failure(); }
      break;
   }
   attr = mlir::DictionaryAttr::get(parser.getBuilder().getContext(), columns);
   return success();
}
static ParseResult parseColumnStateMapping(OpAsmParser& parser, DictionaryAttr& attr) {
   if (parser.parseLBrace()) return failure();
   std::vector<mlir::NamedAttribute> columns;
   while (true) {
      if (!parser.parseOptionalRBrace()) { break; }
      mlir::tuples::ColumnRefAttr columnRefAttr;

      if (parseCustRef(parser, columnRefAttr)) {
         return failure();
      }
      if (parser.parseEqual() || parser.parseGreater()) { return failure(); }
      StringRef colName;
      if (parser.parseKeyword(&colName)) { return failure(); }

      columns.push_back({StringAttr::get(parser.getBuilder().getContext(), colName), columnRefAttr});
      if (!parser.parseOptionalComma()) { continue; }
      if (parser.parseRBrace()) { return failure(); }
      break;
   }
   attr = mlir::DictionaryAttr::get(parser.getBuilder().getContext(), columns);
   return success();
}
static void printStateColumnMapping(OpAsmPrinter& p, mlir::Operation* op, DictionaryAttr attr) {
   p << "{";
   auto first = true;
   for (auto mapping : attr) {
      auto columnName = mapping.getName();
      auto attr = mapping.getValue();
      auto relationDefAttr = attr.dyn_cast_or_null<mlir::tuples::ColumnDefAttr>();
      if (first) {
         first = false;
      } else {
         p << ", ";
      }
      p << columnName.getValue() << " => ";
      printCustDef(p, op, relationDefAttr);
   }
   p << "}";
}
static void printColumnStateMapping(OpAsmPrinter& p, mlir::Operation* op, DictionaryAttr attr) {
   p << "{";
   auto first = true;
   for (auto mapping : attr) {
      auto columnName = mapping.getName();
      auto attr = mapping.getValue();
      auto relationRefAttr = attr.dyn_cast_or_null<mlir::tuples::ColumnRefAttr>();
      if (first) {
         first = false;
      } else {
         p << ", ";
      }
      printCustRef(p, op, relationRefAttr);
      p << " => " << columnName.getValue();
   }
   p << "}";
}
static ParseResult parseCustRegion(OpAsmParser& parser, Region& result) {
   OpAsmParser::Argument predArgument;
   SmallVector<OpAsmParser::Argument, 4> regionArgs;
   SmallVector<Type, 4> argTypes;
   if (parser.parseLParen()) {
      return failure();
   }
   while (true) {
      Type predArgType;
      if (!parser.parseOptionalRParen()) {
         break;
      }
      if (parser.parseArgument(predArgument) || parser.parseColonType(predArgType)) {
         return failure();
      }
      predArgument.type = predArgType;
      regionArgs.push_back(predArgument);
      if (!parser.parseOptionalComma()) { continue; }
      if (parser.parseRParen()) { return failure(); }
      break;
   }

   if (parser.parseRegion(result, regionArgs)) return failure();
   return success();
}
static void printCustRegion(OpAsmPrinter& p, Operation* op, Region& r) {
   p << "(";
   bool first = true;
   for (auto arg : r.front().getArguments()) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      p << arg << ": " << arg.getType();
   }
   p << ")";
   p.printRegion(r, false, true);
}
static ParseResult parseCustDefArr(OpAsmParser& parser, ArrayAttr& attr) {
   std::vector<Attribute> attributes;
   if (parser.parseLSquare()) return failure();
   while (true) {
      if (!parser.parseOptionalRSquare()) { break; }
      mlir::tuples::ColumnDefAttr attrDefAttr;
      if (parseCustDef(parser, attrDefAttr)) {
         return failure();
      }
      attributes.push_back(attrDefAttr);
      if (!parser.parseOptionalComma()) { continue; }
      if (parser.parseRSquare()) { return failure(); }
      break;
   }
   attr = parser.getBuilder().getArrayAttr(attributes);
   return success();
}
static void printCustDefArr(OpAsmPrinter& p, mlir::Operation* op, ArrayAttr arrayAttr) {
   p << "[";
   bool first = true;
   for (auto a : arrayAttr) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      mlir::tuples::ColumnDefAttr parsedSymbolRefAttr = a.dyn_cast<mlir::tuples::ColumnDefAttr>();
      printCustDef(p, op, parsedSymbolRefAttr);
   }
   p << "]";
}

ParseResult mlir::subop::CreateSortedViewOp::parse(::mlir::OpAsmParser& parser, ::mlir::OperationState& result) {
   OpAsmParser::UnresolvedOperand getToSort;
   subop::BufferType vecType;
   if (parser.parseOperand(getToSort) || parser.parseColonType(vecType)) {
      return failure();
   }
   if (parser.resolveOperand(getToSort, vecType, result.operands).failed()) {
      return failure();
   }

   mlir::ArrayAttr sortBy;
   if (parser.parseAttribute(sortBy).failed()) {
      return failure();
   }
   result.addAttribute("sortBy", sortBy);
   std::vector<OpAsmParser::Argument> leftArgs(sortBy.size());
   std::vector<OpAsmParser::Argument> rightArgs(sortBy.size());
   if (parser.parseLParen() || parser.parseLSquare()) {
      return failure();
   }
   for (size_t i = 0; i < sortBy.size(); i++) {
      size_t j = 0;
      for (; j < vecType.getMembers().getNames().size(); j++) {
         if (sortBy[i] == vecType.getMembers().getNames()[j]) {
            break;
         }
      }
      leftArgs[i].type = vecType.getMembers().getTypes()[j].cast<mlir::TypeAttr>().getValue();
      if (i > 0 && parser.parseComma().failed()) return failure();
      if (parser.parseArgument(leftArgs[i])) return failure();
   }
   if (parser.parseRSquare() || parser.parseComma() || parser.parseLSquare()) {
      return failure();
   }
   for (size_t i = 0; i < sortBy.size(); i++) {
      size_t j = 0;
      for (; j < vecType.getMembers().getNames().size(); j++) {
         if (sortBy[i] == vecType.getMembers().getNames()[j]) {
            break;
         }
      }
      rightArgs[i].type = vecType.getMembers().getTypes()[j].cast<mlir::TypeAttr>().getValue();
      if (i > 0 && parser.parseComma().failed()) return failure();
      if (parser.parseArgument(rightArgs[i])) return failure();
   }
   if (parser.parseRSquare() || parser.parseRParen()) {
      return failure();
   }
   std::vector<OpAsmParser::Argument> args;
   args.insert(args.end(), leftArgs.begin(), leftArgs.end());
   args.insert(args.end(), rightArgs.begin(), rightArgs.end());
   Region* body = result.addRegion();
   if (parser.parseRegion(*body, args)) return failure();
   result.types.push_back(mlir::subop::SortedViewType::get(parser.getContext(),vecType));
   return success();
}

void subop::CreateSortedViewOp::print(OpAsmPrinter& p) {
   subop::CreateSortedViewOp& op = *this;
   p << " " << op.getToSort() << " : " << op.getToSort().getType() << " " << op.getSortBy() << " ";
   p << "([";
   bool first = true;
   for (size_t i = 0; i < op.getSortBy().size(); i++) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      p << op.getRegion().front().getArgument(i);
   }
   p << "],[";
   first = true;
   for (size_t i = 0; i < op.getSortBy().size(); i++) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      p << op.getRegion().front().getArgument(op.getSortBy().size() + i);
   }
   p << "])";
   p.printRegion(op.getRegion(), false, true);
}
ParseResult mlir::subop::LookupOrInsertOp::parse(::mlir::OpAsmParser& parser, ::mlir::OperationState& result) {
   OpAsmParser::UnresolvedOperand stream;
   if (parser.parseOperand(stream)) {
      return failure();
   }
   if (parser.resolveOperand(stream, mlir::tuples::TupleStreamType::get(parser.getContext()), result.operands).failed()) {
      return failure();
   }
   OpAsmParser::UnresolvedOperand state;
   if (parser.parseOperand(state)) {
      return failure();
   }

   mlir::ArrayAttr keys;
   if (parseCustRefArr(parser, keys).failed()) {
      return failure();
   }
   result.addAttribute("keys", keys);
   mlir::Type stateType;
   if (parser.parseColonType(stateType).failed()) {
      return failure();
   }
   if (parser.resolveOperand(state, stateType, result.operands).failed()) {
      return failure();
   }
   mlir::tuples::ColumnDefAttr reference;
   if (parseCustDef(parser, reference).failed()) {
      return failure();
   }
   result.addAttribute("ref", reference);
   std::vector<OpAsmParser::Argument> leftArgs(keys.size());
   std::vector<OpAsmParser::Argument> rightArgs(keys.size());
   Region* eqFn = result.addRegion();

   if (parser.parseOptionalKeyword("eq").succeeded()) {
      if (parser.parseColon() || parser.parseLParen() || parser.parseLSquare()) {
         return failure();
      }
      for (size_t i = 0; i < keys.size(); i++) {
         leftArgs[i].type = keys[i].cast<mlir::tuples::ColumnRefAttr>().getColumn().type;
         if (i > 0 && parser.parseComma().failed()) return failure();
         if (parser.parseArgument(leftArgs[i])) return failure();
      }
      if (parser.parseRSquare() || parser.parseComma() || parser.parseLSquare()) {
         return failure();
      }
      for (size_t i = 0; i < keys.size(); i++) {
         rightArgs[i].type = keys[i].cast<mlir::tuples::ColumnRefAttr>().getColumn().type;
         if (i > 0 && parser.parseComma().failed()) return failure();
         if (parser.parseArgument(rightArgs[i])) return failure();
      }
      if (parser.parseRSquare() || parser.parseRParen()) {
         return failure();
      }
      std::vector<OpAsmParser::Argument> args;
      args.insert(args.end(), leftArgs.begin(), leftArgs.end());
      args.insert(args.end(), rightArgs.begin(), rightArgs.end());
      if (parser.parseRegion(*eqFn, args)) return failure();
   }
   Region* initialFn = result.addRegion();

   if (parser.parseOptionalKeyword("initial").succeeded()) {
      if (parser.parseColon()) return failure();
      if (parser.parseRegion(*initialFn, {})) return failure();
   }
   result.addTypes(mlir::tuples::TupleStreamType::get(parser.getContext()));
   return success();
}

void subop::LookupOrInsertOp::print(OpAsmPrinter& p) {
   subop::LookupOrInsertOp& op = *this;
   p << " " << op.getStream() << op.getState() << " ";
   printCustRefArr(p, op, op.getKeys());
   p << " : " << op.getState().getType() << " ";
   printCustDef(p, op, op.getRef());
   if (!op.getEqFn().empty()) {
      p << "eq: ([";
      bool first = true;
      for (size_t i = 0; i < op.getKeys().size(); i++) {
         if (first) {
            first = false;
         } else {
            p << ",";
         }
         p << op.getEqFn().front().getArgument(i);
      }
      p << "],[";
      first = true;
      for (size_t i = 0; i < op.getKeys().size(); i++) {
         if (first) {
            first = false;
         } else {
            p << ",";
         }
         p << op.getEqFn().front().getArgument(op.getKeys().size() + i);
      }
      p << "]) ";
      p.printRegion(op.getEqFn(), false, true);
   }
   if (!op.getInitFn().empty()) {
      p << "initial: ";
      p.printRegion(op.getInitFn(), false, true);
   }
}
ParseResult mlir::subop::LookupOp::parse(::mlir::OpAsmParser& parser, ::mlir::OperationState& result) {
   OpAsmParser::UnresolvedOperand stream;
   if (parser.parseOperand(stream)) {
      return failure();
   }
   if (parser.resolveOperand(stream, mlir::tuples::TupleStreamType::get(parser.getContext()), result.operands).failed()) {
      return failure();
   }
   OpAsmParser::UnresolvedOperand state;
   if (parser.parseOperand(state)) {
      return failure();
   }

   mlir::ArrayAttr keys;
   if (parseCustRefArr(parser, keys).failed()) {
      return failure();
   }
   result.addAttribute("keys", keys);
   mlir::Type stateType;
   if (parser.parseColonType(stateType).failed()) {
      return failure();
   }
   if (parser.resolveOperand(state, stateType, result.operands).failed()) {
      return failure();
   }
   mlir::tuples::ColumnDefAttr reference;
   if (parseCustDef(parser, reference).failed()) {
      return failure();
   }
   result.addAttribute("ref", reference);
   std::vector<OpAsmParser::Argument> leftArgs(keys.size());
   std::vector<OpAsmParser::Argument> rightArgs(keys.size());
   Region* eqFn = result.addRegion();

   if (parser.parseOptionalKeyword("eq").succeeded()) {
      if (parser.parseColon() || parser.parseLParen() || parser.parseLSquare()) {
         return failure();
      }
      for (size_t i = 0; i < keys.size(); i++) {
         leftArgs[i].type = keys[i].cast<mlir::tuples::ColumnRefAttr>().getColumn().type;
         if (i > 0 && parser.parseComma().failed()) return failure();
         if (parser.parseArgument(leftArgs[i])) return failure();
      }
      if (parser.parseRSquare() || parser.parseComma() || parser.parseLSquare()) {
         return failure();
      }
      for (size_t i = 0; i < keys.size(); i++) {
         rightArgs[i].type = keys[i].cast<mlir::tuples::ColumnRefAttr>().getColumn().type;
         if (i > 0 && parser.parseComma().failed()) return failure();
         if (parser.parseArgument(rightArgs[i])) return failure();
      }
      if (parser.parseRSquare() || parser.parseRParen()) {
         return failure();
      }
      std::vector<OpAsmParser::Argument> args;
      args.insert(args.end(), leftArgs.begin(), leftArgs.end());
      args.insert(args.end(), rightArgs.begin(), rightArgs.end());
      if (parser.parseRegion(*eqFn, args)) return failure();
   }
   Region* initialFn = result.addRegion();

   if (parser.parseOptionalKeyword("initial").succeeded()) {
      if (parser.parseColon()) return failure();
      if (parser.parseRegion(*initialFn, {})) return failure();
   }
   result.addTypes(mlir::tuples::TupleStreamType::get(parser.getContext()));
   return success();
}

void subop::LookupOp::print(OpAsmPrinter& p) {
   subop::LookupOp& op = *this;
   p << " " << op.getStream() << op.getState() << " ";
   printCustRefArr(p, op, op.getKeys());
   p << " : " << op.getState().getType() << " ";
   printCustDef(p, op, op.getRef());
   if (!op.getEqFn().empty()) {
      p << "eq: ([";
      bool first = true;
      for (size_t i = 0; i < op.getKeys().size(); i++) {
         if (first) {
            first = false;
         } else {
            p << ",";
         }
         p << op.getEqFn().front().getArgument(i);
      }
      p << "],[";
      first = true;
      for (size_t i = 0; i < op.getKeys().size(); i++) {
         if (first) {
            first = false;
         } else {
            p << ",";
         }
         p << op.getEqFn().front().getArgument(op.getKeys().size() + i);
      }
      p << "]) ";
      p.printRegion(op.getEqFn(), false, true);
   }
   if (!op.getInitFn().empty()) {
      p << "initial: ";
      p.printRegion(op.getInitFn(), false, true);
   }
}
ParseResult mlir::subop::LoopOp::parse(::mlir::OpAsmParser& parser, ::mlir::OperationState& result) {
   llvm::SmallVector<OpAsmParser::UnresolvedOperand> args;
   llvm::SmallVector<Type> argTypes;
   llvm::SmallVector<OpAsmParser::Argument> arguments;
   llvm::SmallVector<Type> argumentTypes;

   if (parser.parseOperandList(args) || parser.parseOptionalColonTypeList(argTypes)) {
      return failure();
   }
   if (parser.resolveOperands(args, argTypes, parser.getCurrentLocation(), result.operands).failed()) {
      return failure();
   }
   if (parser.parseLParen() || parser.parseArgumentList(arguments) || parser.parseRParen() || parser.parseOptionalArrowTypeList(argumentTypes)) {
      return failure();
   }
   if (arguments.size() != argumentTypes.size()) {
      return failure();
   }
   for (auto i = 0ul; i < arguments.size(); i++) {
      arguments[i].type = argumentTypes[i];
   }
   result.types.insert(result.types.end(), argumentTypes.begin(), argumentTypes.end());
   Region* body = result.addRegion();
   if (parser.parseRegion(*body, arguments)) return failure();
   return success();
}
void mlir::subop::LoopOp::print(::mlir::OpAsmPrinter& p) {
   if (!getArgs().empty()) {
      p << getArgs() << " : " << getArgs().getTypes();
   }
   p << " (";
   for (size_t i = 0; i < getRegion().getNumArguments(); i++) {
      if (i != 0) {
         p << " ,";
      }
      p << getRegion().getArguments()[i];
   }
   p << ")";
   if (!getResultTypes().empty()) {
      p << "-> " << getResultTypes();
   }
   p.printRegion(getRegion(), false, true);
}
ParseResult mlir::subop::ReduceOp::parse(::mlir::OpAsmParser& parser, ::mlir::OperationState& result) {
   OpAsmParser::UnresolvedOperand stream;
   if (parser.parseOperand(stream)) {
      return failure();
   }
   if (parser.resolveOperand(stream, mlir::tuples::TupleStreamType::get(parser.getContext()), result.operands).failed()) {
      return failure();
   }
   mlir::tuples::ColumnRefAttr reference;
   if (parseCustRef(parser, reference).failed()) {
      return failure();
   }
   result.addAttribute("ref", reference);

   mlir::ArrayAttr columns;
   if (parseCustRefArr(parser, columns).failed()) {
      return failure();
   }
   result.addAttribute("columns", columns);
   mlir::ArrayAttr members;
   if (parser.parseAttribute(members).failed()) {
      return failure();
   }
   result.addAttribute("members", members);
   std::vector<OpAsmParser::Argument> leftArgs(columns.size());
   std::vector<OpAsmParser::Argument> rightArgs(members.size());
   if (parser.parseLParen() || parser.parseLSquare()) {
      return failure();
   }
   auto referenceType = reference.getColumn().type.cast<mlir::subop::StateEntryReference>();
   auto stateMembers = referenceType.getMembers();
   for (size_t i = 0; i < columns.size(); i++) {
      leftArgs[i].type = columns[i].cast<mlir::tuples::ColumnRefAttr>().getColumn().type;
      if (i > 0 && parser.parseComma().failed()) return failure();
      if (parser.parseArgument(leftArgs[i])) return failure();
   }
   if (parser.parseRSquare() || parser.parseComma() || parser.parseLSquare()) {
      return failure();
   }
   for (size_t i = 0; i < members.size(); i++) {
      size_t j = 0;
      for (; j < stateMembers.getNames().size(); j++) {
         if (members[i] == stateMembers.getNames()[j]) {
            break;
         }
      }
      rightArgs[i].type = stateMembers.getTypes()[j].cast<mlir::TypeAttr>().getValue();
      if (i > 0 && parser.parseComma().failed()) return failure();
      if (parser.parseArgument(rightArgs[i])) return failure();
   }

   if (parser.parseRSquare() || parser.parseRParen()) {
      return failure();
   }
   std::vector<OpAsmParser::Argument> args;
   args.insert(args.end(), leftArgs.begin(), leftArgs.end());
   args.insert(args.end(), rightArgs.begin(), rightArgs.end());
   Region* body = result.addRegion();
   if (parser.parseRegion(*body, args)) return failure();
   return success();
}

void subop::ReduceOp::print(OpAsmPrinter& p) {
   subop::ReduceOp& op = *this;
   p << " " << op.getStream() << " ";
   printCustRef(p, op, op.getRef());
   printCustRefArr(p, op, op.getColumns());
   p << " " << op.getMembers() << " ";
   p << "([";
   bool first = true;
   for (size_t i = 0; i < op.getColumns().size(); i++) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      p << op.getRegion().front().getArgument(i);
   }
   p << "],[";
   first = true;
   for (size_t i = 0; i < op.getMembers().size(); i++) {
      if (first) {
         first = false;
      } else {
         p << ",";
      }
      p << op.getRegion().front().getArgument(op.getColumns().size() + i);
   }
   p << "])";
   p.printRegion(op.getRegion(), false, true);
}

ParseResult mlir::subop::NestedMapOp::parse(::mlir::OpAsmParser& parser, ::mlir::OperationState& result) {
   OpAsmParser::UnresolvedOperand stream;
   if (parser.parseOperand(stream)) {
      return failure();
   }
   if (parser.resolveOperand(stream, mlir::tuples::TupleStreamType::get(parser.getContext()), result.operands).failed()) {
      return failure();
   }

   OpAsmParser::Argument streamArg;
   std::vector<OpAsmParser::Argument> parameterArgs;
   mlir::ArrayAttr parameters;
   if (parseCustRefArr(parser, parameters).failed()) {
      return failure();
   }
   result.addAttribute("parameters", parameters);
   if (parser.parseLParen()) {
      return failure();
   }
   streamArg.type = mlir::tuples::TupleType::get(parser.getContext());
   if (parser.parseArgument(streamArg).failed()) {
      return failure();
   }
   for (auto x : parameters) {
      OpAsmParser::Argument arg;
      arg.type = x.cast<mlir::tuples::ColumnRefAttr>().getColumn().type;
      if (parser.parseComma() || parser.parseArgument(arg)) return failure();
      parameterArgs.push_back(arg);
   }
   if (parser.parseRParen()) {
      return failure();
   }
   Region* body = result.addRegion();
   std::vector<OpAsmParser::Argument> args;
   args.push_back(streamArg);
   args.insert(args.end(), parameterArgs.begin(), parameterArgs.end());
   if (parser.parseRegion(*body, args)) return failure();
   result.addTypes(mlir::tuples::TupleStreamType::get(parser.getContext()));
   return success();
}

void subop::NestedMapOp::print(OpAsmPrinter& p) {
   subop::NestedMapOp& op = *this;
   p << " " << op.getStream() << " ";
   printCustRefArr(p, this->getOperation(), getParameters());
   p << " (";
   p.printOperands(op.getRegion().front().getArguments());
   p << ") ";
   p.printRegion(op.getRegion(), false, true);
}
ParseResult mlir::subop::GenerateOp::parse(::mlir::OpAsmParser& parser, ::mlir::OperationState& result) {
   mlir::ArrayAttr createdColumns;
   if (parseCustDefArr(parser, createdColumns).failed()) {
      return failure();
   }
   result.addAttribute("generated_columns", createdColumns);

   Region* body = result.addRegion();
   if (parser.parseRegion(*body, {})) return failure();
   result.addTypes(mlir::tuples::TupleStreamType::get(parser.getContext()));
   return success();
}

void subop::GenerateOp::print(OpAsmPrinter& p) {
   subop::GenerateOp& op = *this;
   printCustDefArr(p, this->getOperation(), getGeneratedColumns());
   p.printRegion(op.getRegion(), false, true);
}

std::vector<std::string> subop::ScanOp::getReadMembers() {
   std::vector<std::string> res;
   for (auto x : getMapping()) {
      res.push_back(x.getName().str());
   }
   return res;
}
std::vector<std::string> subop::MaterializeOp::getWrittenMembers() {
   std::vector<std::string> res;
   for (auto x : getMapping()) {
      res.push_back(x.getName().str());
   }
   return res;
}
std::vector<std::string> subop::NestedMapOp::getReadMembers() {
   std::vector<std::string> res;
   this->getRegion().walk([&](mlir::subop::SubOperator subop) {
      auto read = subop.getReadMembers();
      res.insert(res.end(), read.begin(), read.end());
   });
   return res;
}
std::vector<std::string> subop::NestedMapOp::getWrittenMembers() {
   std::vector<std::string> res;
   this->getRegion().walk([&](mlir::subop::SubOperator subop) {
      auto written = subop.getWrittenMembers();
      res.insert(res.end(), written.begin(), written.end());
   });
   return res;
}
std::vector<std::string> subop::LoopOp::getReadMembers() {
   std::vector<std::string> res;
   this->getRegion().walk([&](mlir::subop::SubOperator subop) {
      auto read = subop.getReadMembers();
      res.insert(res.end(), read.begin(), read.end());
   });
   return res;
}
std::vector<std::string> subop::LoopOp::getWrittenMembers() {
   std::vector<std::string> res;
   this->getRegion().walk([&](mlir::subop::SubOperator subop) {
      auto written = subop.getWrittenMembers();
      res.insert(res.end(), written.begin(), written.end());
   });
   return res;
}
std::vector<std::string> subop::CreateSortedViewOp::getWrittenMembers() {
   std::vector<std::string> res;
   for (auto x : getToSort().getType().cast<mlir::subop::BufferType>().getMembers().getNames()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::CreateHashIndexedView::getWrittenMembers() {
   return {getLinkMember().str(),getHashMember().str()};
}
std::vector<std::string> subop::CreateHashIndexedView::getReadMembers() {
   return {getHashMember().str()};
}
std::vector<std::string> subop::MaintainOp::getWrittenMembers() {
   std::vector<std::string> res;
   for (auto x : getState().getType().cast<mlir::subop::State>().getMembers().getNames()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::MaintainOp::getReadMembers() {
   std::vector<std::string> res;
   for (auto x : getState().getType().cast<mlir::subop::State>().getMembers().getNames()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::CreateSortedViewOp::getReadMembers() {
   std::vector<std::string> res;
   for (auto x : getSortBy()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::ReduceOp::getWrittenMembers() {
   std::vector<std::string> res;
   for (auto x : getMembers()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::ReduceOp::getReadMembers() {
   std::vector<std::string> res;
   for (auto x : getMembers()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::ScatterOp::getWrittenMembers() {
   std::vector<std::string> res;
   for (auto x : getMapping()) {
      res.push_back(x.getName().str());
   }
   return res;
}
std::vector<std::string> subop::LookupOrInsertOp::getWrittenMembers() {
   std::vector<std::string> res;
   for (auto x : getState().getType().cast<mlir::subop::State>().getMembers().getNames()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::LookupOp::getReadMembers() {
   std::vector<std::string> res;
   for (auto x : getState().getType().cast<mlir::subop::State>().getMembers().getNames()) {
      res.push_back(x.cast<mlir::StringAttr>().str());
   }
   return res;
}
std::vector<std::string> subop::GatherOp::getReadMembers() {
   std::vector<std::string> res;
   for (auto x : getMapping()) {
      res.push_back(x.getName().str());
   }
   return res;
}
#define GET_OP_CLASSES
#include "mlir/Dialect/SubOperator/SubOperatorOps.cpp.inc"

#include "mlir/Dialect/SubOperator/SubOperatorOpsEnums.cpp.inc"
