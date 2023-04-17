#include "runtime/ExecutionContext.h"
#include "runtime/Database.h"
#include <iostream>
runtime::Database* runtime::ExecutionContext::getDatabase() {
   return db.get();
}

void runtime::ExecutionContext::setResult(uint32_t id, uint8_t* ptr) {
   if (states.contains(ptr)) {
      states.erase(ptr);
   }
   results[id] = ptr;
}

void runtime::ExecutionContext::setTupleCount(uint32_t id, int64_t tupleCount) {
   tupleCounts[id] = tupleCount;
}
void runtime::ExecutionContext::reset() {
   for (auto s : states) {
      s.second.freeFn(s.second.ptr);
   }
   states.clear();
}
runtime::ExecutionContext::~ExecutionContext() {
   reset();
}