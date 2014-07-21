/*    Copyright 2014 MongoDB Inc.
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

#if __cplusplus >= 201103L
#  define EV_DECLTYPE decltype
#elif defined(__GNUC__)
#  define EV_DECLTYPE __typeof__
#elif defined(_MSC_VER) && _MSC_VER >= 1600
#  define EV_DECLTYPE decltype
#else
#  error "Need a compiler with some kind of decltype support"
#endif

#define EV_DECL_NS_BEGIN(name) \
    namespace name { \
        class cview; \
        class view; \
        class value;

#define EV_DECL_STRUCT \
    struct layout_type

#define EV_DECL_CONST_METHODS_BEGIN \
    template <class storage_type_t> \
    class const_methods { \
    protected: \
        storage_type_t _storage; \
    private:

#define EV_DECL_CONST_METHODS_END };
        
#define EV_DECL_MUTABLE_METHODS_BEGIN \
    template <class storage_type_t> \
    class mutable_methods : public const_methods<storage_type_t> { \
        typedef const_methods<storage_type_t> base; \
    private:

#define EV_DECL_MUTABLE_METHODS_END };

#define EV_DECL_NS_END \
    class cview : public const_methods<encoded_value::const_pointer_storage_type<layout_type> > { \
        friend const void* view2ptr(cview c); \
    public: \
        cview(const char* base = NULL) { \
            _storage.bytes = base; \
        } \
    }; \
    class view : public mutable_methods<encoded_value::pointer_storage_type<layout_type> > { \
    public: \
        view(char* base = NULL) { \
            _storage.bytes = base; \
        } \
        operator cview() const { \
            return _storage.bytes; \
        } \
    }; \
    class value : public mutable_methods<encoded_value::array_storage_type<layout_type> > { \
    public: \
        value() { } \
        value(uint8_t x) { \
            std::memset(_storage.bytes, x, sizeof(_storage.bytes)); \
        } \
        operator class view() { \
            return _storage.bytes; \
        } \
        operator cview() const { \
            return _storage.bytes; \
        } \
        typedef cview ev_cview_type; \
        typedef view ev_view_type; \
    }; \
    inline const void* view2ptr(cview c) { \
        return c._storage.bytes; \
    } \
};

#define EV_DECLTYPE_MEMBER(name) \
    EV_DECLTYPE(((layout_type*)0)->name)

#define EV_OFFSETOF(name) \
    ((std::size_t) &(((layout_type*)0)->name))
    //offsetof(layout_type, name)

#define EV_DECL_ACCESSOR(name) \
    EV_DECLTYPE_MEMBER(name) name() const { \
        return this->_storage.template read<EV_DECLTYPE_MEMBER(name)>(EV_OFFSETOF(name)); \
    }

#define EV_DECL_ARRAY_ACCESSOR(name) \
    EV_DECLTYPE_MEMBER(name[0]) name(std::size_t n) const { \
        return this->_storage.template read<EV_DECLTYPE_MEMBER(name[n])>(EV_OFFSETOF(name[n])); \
    }

#define EV_DECL_RAW_ACCESSOR(name) \
    const char * name() const { \
        return this->_storage.view(EV_OFFSETOF(name)); \
    }

#define EV_DECL_VIEW_ACCESSOR(name) \
    EV_DECLTYPE_MEMBER(name)::ev_cview_type name() const { \
        return this->_storage.view(EV_OFFSETOF(name)); \
    }

#define EV_DECL_MUTATOR(name) \
    using base::name; \
    void name(EV_DECLTYPE_MEMBER(name) value) { \
        return this->_storage.write(EV_OFFSETOF(name), value); \
    }

#define EV_DECL_ARRAY_MUTATOR(name) \
    using base::name; \
    void name(std::size_t n, EV_DECLTYPE_MEMBER(name[0]) value) { \
        return this->_storage.write(EV_OFFSETOF(name[n]), value); \
    }

#define EV_DECL_RAW_MUTATOR(name) \
    using base::name; \
    char * name() { \
        return this->_storage.view(EV_OFFSETOF(name)); \
    }

#define EV_DECL_VIEW_MUTATOR(name) \
    using base::name; \
    EV_DECLTYPE_MEMBER(name)::ev_view_type name() { \
        return this->_storage.view(EV_OFFSETOF(name)); \
    }

#define EV_INSTANTIATE(name) \
    template class name::const_methods<encoded_value::array_storage_type<name::layout_type> >; \
    template class name::mutable_methods<encoded_value::array_storage_type<name::layout_type> >; \
    template class name::const_methods<encoded_value::pointer_storage_type<name::layout_type> >; \
    template class name::mutable_methods<encoded_value::pointer_storage_type<name::layout_type> >; \
    template class name::const_methods<encoded_value::const_pointer_storage_type<name::layout_type> >;
