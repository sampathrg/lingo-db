#ifndef RUNTIME_HELPERS_H
#define RUNTIME_HELPERS_H
#include "string.h"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>
#define EXPORT extern "C" __attribute__((visibility("default")))
#define INLINE __attribute__((always_inline))
namespace runtime {
class Str {
   char* pointer;
   size_t size;

   public:
   Str(char* ptr, size_t len) : pointer(ptr), size(len) {}
   operator std::string() { return std::string(pointer, size); }
   std::string str() { return std::string(pointer, size); }
   size_t len() {
      return size;
   }
   char* data() {
      return pointer;
   }
};
class Bytes {
   uint8_t* pointer;
   size_t size;

   public:
   Bytes(uint8_t* ptr, size_t bytes) : pointer(ptr), size(bytes) {}
   uint8_t* getPtr() {
      return pointer;
   }
   size_t getSize() {
      return size;
   }
};
static uint64_t UNALIGNED_LOAD64(const uint8_t* p) {
   uint64_t result;
   memcpy(&result, p, sizeof(result));
   return result;
}

static uint32_t UNALIGNED_LOAD32(const uint8_t* p) {
   uint32_t result;
   memcpy(&result, p, sizeof(result));
   return result;
}
static bool CACHELINE_REMAINS_8(const uint8_t* p) {
   return (reinterpret_cast<uintptr_t>(p) & 63) <= 56;
}
static uint64_t READ_8_PAD_ZERO(const uint8_t* p, uint32_t len) {
   if (len == 0) return 0; //do not dereference!
   if (len >= 8) return UNALIGNED_LOAD64(p); //best case
   if (CACHELINE_REMAINS_8(p)) {
      auto bytes = UNALIGNED_LOAD64(p);
      auto shift = len * 8;
      auto mask = ~((~0ull) << shift);
      auto ret = bytes & mask; //we can load bytes, but have to shift for invalid bytes
      return ret;
   }
   return UNALIGNED_LOAD64(p + len - 8) >> (64 - len * 8);
}

class VarLen32 {
   static constexpr uint32_t SHORT_LEN = 12;
   uint32_t len;
   union {
      uint8_t bytes[SHORT_LEN];
      struct __attribute__((__packed__)) {
         uint32_t first4;
         uint64_t last8;
      };
   };

   private:
   void storePtr(uint8_t* ptr) {
      uint8_t** ptrloc = reinterpret_cast<uint8_t**>((&bytes[4]));
      *ptrloc = ptr;
   }

   public:
   VarLen32(uint8_t* ptr, uint32_t len) : len(len) {
      if (len > SHORT_LEN) {
         //todo: copy for temporary strings
         storePtr(ptr);
      } else if (len > 8) {
         this->first4 = UNALIGNED_LOAD32(ptr);
         this->last8 = UNALIGNED_LOAD64(ptr + len - 8);
         uint32_t duplicate = 12 - len;
         this->last8 >>= (duplicate * 8);
      } else {
         auto bytes = READ_8_PAD_ZERO(ptr, len);
         this->first4 = bytes;
         this->last8 = bytes >> 32;
      }
   }
   uint8_t* getPtr() {
      if (len <= SHORT_LEN) {
         return bytes;
      } else {
         return reinterpret_cast<uint8_t*>(*(uintptr_t*) (&bytes[4]));
      }
   }
   char* data() {
      return (char*) getPtr();
   }
   uint32_t getLen() {
      return len;
   }

   __int128 asI128() {
      return *(reinterpret_cast<__int128*>(this));
   }

   operator std::string() { return std::string((char*) getPtr(), len); }
   std::string str() { return std::string((char*) getPtr(), len); }
};
} // end namespace runtime
#endif // RUNTIME_HELPERS_H
