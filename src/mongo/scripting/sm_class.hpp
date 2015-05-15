/*    Copyright 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include <cstddef>
#include <type_traits>

#define JS_USE_CUSTOM_ALLOCATOR

#include "jsapi.h"
#include "jscustomallocator.h"

#define HAS_METHOD_T(name) \
template <typename T> \
class has_##name \
{ \
private: \
    template <typename U> \
    class check \
    { }; \
    template <typename C> \
    static char f(check<decltype(&C::name)>*); \
    template <typename C> \
    static long f(...); \
public: \
    static const bool value = (sizeof(f<T>(0)) == sizeof(char)); \
}; \
template <typename T, typename = void> \
struct name##_or_nullptr \
{ \
    static constexpr std::nullptr_t value = nullptr; \
}; \
template <typename T> \
struct name##_or_nullptr<T, typename std::enable_if<smUtils::has_##name<T>::value>::type> \
{ \
    static constexpr auto value = &T::name; \
};

#define INSTALL_POINTER(name) \
    static constexpr auto addrOf##name = smUtils::name##_or_nullptr<T>::value;

#define METHODS(...) \

namespace mongo {

    namespace smUtils {
        HAS_METHOD_T(addProperty)
        HAS_METHOD_T(delProperty)
        HAS_METHOD_T(getProperty)
        HAS_METHOD_T(setProperty)
        HAS_METHOD_T(enumerate)
        HAS_METHOD_T(resolve)
        HAS_METHOD_T(convert)
        HAS_METHOD_T(finalize)
        HAS_METHOD_T(call)
        HAS_METHOD_T(hasInstance)
        HAS_METHOD_T(construct)
        HAS_METHOD_T(trace)
    }

    template <typename T>
    class SMClass {
        INSTALL_POINTER(addProperty)
        INSTALL_POINTER(delProperty)
        INSTALL_POINTER(getProperty)
        INSTALL_POINTER(setProperty)
        INSTALL_POINTER(enumerate)
        INSTALL_POINTER(resolve)
        INSTALL_POINTER(convert)
        INSTALL_POINTER(finalize)
        INSTALL_POINTER(call)
        INSTALL_POINTER(hasInstance)
        INSTALL_POINTER(construct)
        INSTALL_POINTER(trace)
    public:

        const JSClass jsclass = {
            T::className,
            T::classFlags,
            addrOfaddProperty,
            addrOfdelProperty,
            addrOfgetProperty,
            addrOfsetProperty,
            addrOfenumerate,
            addrOfresolve,
            addrOfconvert,
            addrOffinalize,
            addrOfcall,
            addrOfhasInstance,
            addrOfconstruct,
            addrOftrace,
        };

        SMClass(JSContext* context) :
            _context(context),
            _proto(_context)
        {}

        void install(JS::HandleObject global) {
            JS::RootedObject proto(_context);
            JS::RootedObject parent(_context);

            _proto.set(JS_InitClass(
                _context,
                global,
                proto,
                &jsclass,
                addrOfconstruct,
                0,
                nullptr,
                T::methods,
                nullptr,
                nullptr
            ));
        }

        void newInstance(JS::MutableHandleObject out) {
            JS::AutoValueVector args(_context);

            newInstance(args, out);
        }

        void newInstance(const JS::HandleValueArray& args, JS::MutableHandleObject out) {
            out.set(JS_New(_context, _proto, args));
        }

        void newInstance(JS::MutableHandleValue out) {
            JS::AutoValueVector args(_context);

            newInstance(args, out);
        }

        void newInstance(const JS::HandleValueArray& args, JS::MutableHandleValue out) {
            out.setObjectOrNull(JS_New(_context, _proto, args));
        }

        bool instanceOf(JS::HandleObject obj) {
            return JS_InstanceOf(_context, obj, &jsclass, nullptr);
        }

        bool instanceOf(JS::HandleValue value) {
            JS::RootedObject obj(_context, value.toObjectOrNull());

            return instanceOf(obj);
        }

    private:

        JSContext* _context;
        JS::PersistentRootedObject _proto;
    };
}
