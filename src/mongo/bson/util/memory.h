/*    Copyright 2014 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <cstring>

#if MONGO_HAVE_IS_TRIVIALLY_COPYABLE
#include <type_traits>
#endif

namespace mongo {

    enum MemoryAccessStrategy {
        kMemoryAccessStrategyReinterpret = 0,
        kMemoryAccessStrategyMemcpy = 1
    };

    const MemoryAccessStrategy kDefaultMemoryAccessStrategy =
#if defined(MONGO_USE_REINTERPRET_CAST_MEMORY_ACCESS_STRATEGY)
        kMemoryAccessStrategyReinterpret;
#elif defined(MONGO_USE_MEMCPY_MEMORY_ACCESS_STRATEGY)
        kMemoryAccessStrategyMemcpy;
#else
#error Unknown memory access strategy
#endif

    template<typename T>
    struct trivially_copyable_concept {
        trivially_copyable_concept() {
#if MONGO_HAVE_IS_TRIVIALLY_COPYABLE
            static_assert(
                std::is_trivially_copyable<T>::value,
                "Cannot use Memory[Reader|Writer] for non-trivially-copyable types"
                );
#endif // __cplusplus >= 201103L
        }
    };

    template<MemoryAccessStrategy strategy>
    struct StrategizedMemoryReaderBase;

    template<>
    struct StrategizedMemoryReaderBase<kMemoryAccessStrategyReinterpret> {
        template<typename T>
        static inline void read(T* const target, const void* const source) {
            trivially_copyable_concept<T> check;
            *target = *reinterpret_cast<const T* const>(source);
        }
    };

    template<>
    struct StrategizedMemoryReaderBase<kMemoryAccessStrategyMemcpy> {
        template<typename T>
        static inline void read(T* const target, const void* const source) {
            trivially_copyable_concept<T> check;
            std::memcpy(target, source, sizeof(*target));
        }
    };

    template<MemoryAccessStrategy strategy>
    struct StrategizedMemoryReader : StrategizedMemoryReaderBase<strategy> {

        using StrategizedMemoryReaderBase<strategy>::read;

        template<typename T>
        static inline T read(const void* source) {
            T t;
            read(&t, source);
            return t;
        }
    };

    typedef StrategizedMemoryReader<kDefaultMemoryAccessStrategy> MemoryReader;

    template<MemoryAccessStrategy strategy>
    struct StrategizedMemoryWriter;

    template<>
    struct StrategizedMemoryWriter<kMemoryAccessStrategyMemcpy> {
        template<typename T>
        static inline void write(void* const target, const T* const source) {
            trivially_copyable_concept<T> check;
            std::memcpy(target, source, sizeof(*source));
        }
    };

    template<>
    struct StrategizedMemoryWriter<kMemoryAccessStrategyReinterpret> {
        template<typename T>
        static inline void write(void* const target, const T* const source) {
            trivially_copyable_concept<T> check;
            *reinterpret_cast<T*>(target) = *source;
        }
    };

    typedef StrategizedMemoryWriter<kDefaultMemoryAccessStrategy> MemoryWriter;

    template<typename T>
    class ValueReader {
    public:
        inline ValueReader(T& t)
            : _t(t) {
        }

        inline void readFrom(const void* const source) {
            MemoryReader::read(&_t, source);
        }

    private:
        T& _t;
    };

    template<typename T>
    inline ValueReader<T> value_reader(T& t) {
        return ValueReader<T>(t);
    }

    template<typename T>
    class ValueWriter {
    public:
        inline ValueWriter(const T& t)
            : _t(t) {
        }

        inline void writeTo(void* const target) {
            MemoryWriter::write(target, &_t);
        }

    private:
        const T& _t;
    };

    template<typename T>
    inline ValueWriter<T> value_writer(const T& t) {
        return ValueWriter<T>(t);
    }

    template<typename T>
    class ValueWrapper {
    public:
        inline ValueWrapper(char * ptr) : _ptr(ptr) { }

        inline T get() const {
            return MemoryReader::read<T>(_ptr);
        }

        inline void set(const T& t) {
            MemoryWriter::write(_ptr, &t);
        }

        inline char * ptr() const {
            return _ptr;
        }

        inline size_t size() const {
            return sizeof(T);
        }

        inline ValueWrapper offset(int i) const {
            return ValueWrapper(_ptr + (sizeof(T) * i));
        }

    private:
        char * _ptr;
    };

} // namespace mongo
