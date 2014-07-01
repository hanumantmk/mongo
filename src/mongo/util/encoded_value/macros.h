/**
 *    Copyright (C) 2014 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#define ENCODED_VALUE_CONST_METHODS_NO_CONSTRUCTORS_BEGIN \
    template <class T> \
    class T_CReference : public T {

#define ENCODED_VALUE_CONST_METHODS_BEGIN \
    ENCODED_VALUE_CONST_METHODS_NO_CONSTRUCTORS_BEGIN \
    public: \
        T_CReference(const char * _ptr) { this->storage = _ptr; } \
        T_CReference() {} \
    private:

#define ENCODED_VALUE_CONST_METHODS_END };

#define ENCODED_VALUE_REFERENCE_METHODS_NO_CONSTRUCTORS_BEGIN \
    template <class T> \
    class T_Reference : public T_CReference<T> {

#define ENCODED_VALUE_REFERENCE_METHODS_BEGIN \
    ENCODED_VALUE_REFERENCE_METHODS_NO_CONSTRUCTORS_BEGIN \
    public: \
        T_Reference(char * _ptr) { this->storage = _ptr; } \
        T_Reference() {} \
    private:

#define ENCODED_VALUE_REFERENCE_METHODS_END };

#define ENCODED_VALUE_VALUE_METHODS_NO_CONSTRUCTORS_BEGIN \
    template <class T> \
    class T_Value : public T_Reference<T> {

#define ENCODED_VALUE_VALUE_METHODS_BEGIN \
    ENCODED_VALUE_VALUE_METHODS_NO_CONSTRUCTORS_BEGIN \
    public: \
        T_Value(const char * _ptr) { memcpy(this->storage, _ptr, this->size); } \
        T_Value() {} \
    private:

#define ENCODED_VALUE_VALUE_METHODS_END };

#define ENCODED_VALUE_WRAPPER_CLASS_BEGIN(name, sc) \
template <encoded_value::endian::ConvertEndian convertEndian = encoded_value::endian::kDefault> \
class name { \
public:\
    typedef sc<convertEndian> superclass; \
    typedef name<convertEndian> thisclass; \
    template <class T> class T_CReference; \
    template <class T> class T_Reference; \
    template <class T> class T_Value; \
    typedef T_CReference<typename superclass::CReference> CReference; \
    typedef T_Reference<typename superclass::Reference> Reference; \
    typedef T_Value<typename superclass::Value> Value; \
    typedef encoded_value::Impl::Pointer<encoded_value::Meta::EV<thisclass>, Reference, char * > Pointer; \
    typedef encoded_value::Impl::Pointer<encoded_value::Meta::EV<thisclass>, CReference, const char * > CPointer; \
    static const int size = superclass::size; \

#define ENCODED_VALUE_WRAPPER_CLASS_END \
};

#define ENCODED_VALUE_REFERENCE_METHOD(rval, name) \
    template<encoded_value::endian::ConvertEndian convertEndian> \
    template<class T> \
    rval name<convertEndian>::template T_Reference<T>

#define ENCODED_VALUE_CONST_METHOD(rval, name) \
    template<encoded_value::endian::ConvertEndian convertEndian> \
    template<class T> \
    rval name<convertEndian>::template T_CReference<T>

#define ENCODED_VALUE_VALUE_METHOD(rval, name) \
    template<encoded_value::endian::ConvertEndian convertEndian> \
    template<class T> \
    rval name<convertEndian>::template T_Value<T>

#define ENCODED_VALUE_INSTANTIATE(name, ce) \
    template class name<encoded_value::endian::ce>; \
    template class name<encoded_value::endian::ce>::template T_Reference<name<encoded_value::endian::ce>::superclass::Reference>; \
    template class name<encoded_value::endian::ce>::template T_CReference<name<encoded_value::endian::ce>::superclass::CReference>; \
    template class name<encoded_value::endian::ce>::template T_Value<name<encoded_value::endian::ce>::superclass::Value>;
