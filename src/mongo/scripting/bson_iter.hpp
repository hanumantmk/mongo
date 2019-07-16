#pragma once

#include <cstring>
#include <cstdint>

class bson_iter {
   public:
   bson_iter (const uint8_t* b) : bytes(b), ptr(b) {
      uint32_t x = 0;
      std::memcpy (&x, b, 4);

      l = x;
   }

   const char * utf8() const {
      return (const char *)ptr + name_skip() + 4;
   }

   int32_t int32() const {
      int32_t i32 = 0;
      std::memcpy(&i32, ptr + name_skip(), 4);

      return i32;
   }

   int64_t int64() const {
      int64_t i64 = 0;
      std::memcpy(&i64, ptr + name_skip(), 8);

      return i64;
   }

   double dbl() const {
      double d = 0;
      std::memcpy(&d, ptr + name_skip(), 8);

      return d;
   }

   bool bl() const {
      return ptr[name_skip()];
   }

   bson_iter recurse() const {
      return bson_iter(ptr + name_skip());
   }

   uint8_t type() const {
      return ptr[0];
   }

   const char * key() const {
      return (const char *)ptr + 1;
   }

   const uint8_t* keyAndValue(size_t* out) const {
       auto next = computeNext();
       *out = next - ptr;
       return ptr;
   }

   bool next() {
      ptr = computeNext();

      return *ptr != 0;
   }

   const uint8_t* computeNext() const {
      if (ptr == bytes) {
         return ptr + 4;
      } else {
         uint32_t ns = name_skip();

         switch (*ptr) {
            case 0x01:
               ns += 8;
               break;
            case 0x02:
            {
               const uint8_t *p = ptr + ns;
               uint32_t x = 0;
               std::memcpy (&x, p, 4);

               ns += x + 4;

               break;
            }
            case 0x03:
            case 0x04:
            {
               const uint8_t *p = ptr + ns;
               uint32_t x = 0;
               std::memcpy (&x, p, 4);

               ns += x;

               break;
            }
            case 0x07:
               ns += 11;
            case 0x08:
               ns++;
               break;
            case 0x0a:
               break;
            case 0x10:
               ns += 4;
               break;
            case 0x12:
               ns += 8;
               break;
         }

         return ptr + ns;
      }
   }

   uint32_t len() const {
      return l;
   }

   private:

   uint32_t name_skip() const {
      const uint8_t *p;

      for (p = ptr + 1; *p; p++) {}

      return (1 + (p - ptr));
   }

   const uint8_t *bytes;
   const uint8_t *ptr;
   uint32_t l;
};
