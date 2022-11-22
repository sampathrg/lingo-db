#ifndef RUNTIME_GROWINGBUFFER_H
#define RUNTIME_GROWINGBUFFER_H
#include "runtime/Buffer.h"
namespace runtime {
class GrowingBuffer {
   runtime::FlexibleBuffer values;

   public:
   GrowingBuffer(size_t cap, size_t typeSize) : values(cap, typeSize) {}
   uint8_t* insert();
   static GrowingBuffer* create(size_t sizeOfType, size_t initialCapacity);
   size_t getLen() const;
   size_t getTypeSize() const;
   runtime::Buffer sort(bool (*compareFn)(uint8_t*, uint8_t*));
   static void destroy(GrowingBuffer* vec);
   BufferIterator* createIterator();
   runtime::FlexibleBuffer& getValues() { return values; }
};
} // end namespace runtime
#endif //RUNTIME_GROWINGBUFFER_H
