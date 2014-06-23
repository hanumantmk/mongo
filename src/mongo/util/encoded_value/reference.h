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

/* Reference provides a synthetic api that mimics regular reference semantics, but
 * has a overloads for assignment and can decay to a real underlying type.  The
 * basic idea is that you can use one of these just like the base type, but
 * reads and writes will be strategized appropriately.
 *
 * The template parameters are used as follows:
 *
 * M - The meta class parameter wraps up all the needed metadata to actually
 *     read and write through a char *.  This includes endian conversions
 *
 * S - The storage for the pointer.  In practice this must be char * or const
 *     char *.  I.e. a const pointer or not
 *
 * */

namespace mongo {
namespace encoded_value {
namespace Impl {

    template <class M, class R, typename S> class Pointer;

    template <class M, typename S>
    class Reference {
        typedef typename M::type T;
    public:

        /* Entry point for all writes */
        Reference& operator=(const T& other) {
            M::writeTo(other, _ptr);

            return *this;
        }

        /* all reads */
        operator T() const {
            T t;
            M::readFrom(t, _ptr);

            return t;
        }

        /* here come all the interesting user overloadable assignments... */
        Reference& operator+=(const T& other) {
            operator=(operator T() + other);

            return *this;
        }

        Reference& operator-=(const T& other) {
            operator=(operator T() - other);

            return *this;
        }

        Reference& operator*=(const T& other) {
            operator=(operator T() * other);

            return *this;
        }

        Reference& operator/=(const T& other) {
            operator=(operator T() / other);

            return *this;
        }

        Reference& operator%=(const T& other) {
            operator=(operator T() % other);

            return *this;
        }

        Reference& operator&=(const T& other) {
            operator=(operator T() & other);

            return *this;
        }

        Reference& operator|=(const T& other) {
            operator=(operator T() | other);

            return *this;
        }

        Reference& operator^=(const T& other) {
            operator=(operator T() ^ other);

            return *this;
        }

        Reference& operator<<=(const T& other) {
            operator=(operator T() << other);

            return *this;
        }

        Reference& operator>>=(const T& other) {
            operator=(operator T() >> other);

            return *this;
        }

        Reference& operator++() {
            operator+=(1);

            return *this;
        }

        T operator++(int) {
            T tmp = operator T();
            operator++();
            return tmp;
        }

        Reference& operator--() {
            operator-=(1);

            return *this;
        }

        T operator--(int) {
            T tmp = operator T();
            operator--();
            return tmp;
        }

        S ptr() const {
            return _ptr;
        }

        Reference(const Reference& other) {
            _ptr = other._ptr;
        }

        Reference(S ptr) : _ptr(ptr) { }

        /* This is a little sketchy, but it allows for offseting in generated
         * structs, or into arrays of Ref's.  There's a stdx::addressof to work
         * around it as necessary */
        Impl::Pointer<M, Reference, S> operator&() const {
            return Impl::Pointer<M, Reference, S>(_ptr);
        }

    private:
        Reference();
        Reference& operator=(const Reference& other);

        S _ptr;
    };

} // namespace Impl
} // namespace encoded_value
} // namespace mongo
