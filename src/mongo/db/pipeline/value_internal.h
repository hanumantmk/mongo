/**
 * Copyright (c) 2012 10gen Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects for
 * all of the code used other than as permitted herein. If you modify file(s)
 * with this exception, you may extend this exception to your version of the
 * file(s), but you are not obligated to do so. If you do not wish to do so,
 * delete this exception statement from your version. If you delete this
 * exception statement from all source files in the program, then also delete
 * it in the license file.
 */

#pragma once

#include <algorithm>
#include "bson/bsonobj.h"
#include "bson/bsontypes.h"
#include "bson/bsonmisc.h"
#include "bson/oid.h"
#include "util/intrusive_counter.h"
#include "mongo/bson/optime.h"


namespace mongo {
    class Document;
    class DocumentStorage;
    class Value;

    //TODO: a MutableVector, similar to MutableDocument
    /// A heap-allocated reference-counted std::vector
    class RCVector : public RefCountable {
    public:
        RCVector() {}
        RCVector(const vector<Value>& v) :vec(v) {}
        vector<Value> vec;
    };

    class RCCodeWScope : public RefCountable {
    public:
        RCCodeWScope(const string& str, BSONObj obj) :code(str), scope(obj.getOwned()) {}
        const string code;
        const BSONObj scope; // Not worth converting to Document for now
    };

    class RCDBRef : public RefCountable {
    public:
        RCDBRef(const string& str, const OID& o) :ns(str), oid(o) {}
        const string ns;
        const OID oid;
    };

    class ValueStorage {
#include "db/pipeline/value_storage_data.h"

    ValueStorageData vsd;

    public:
        // Note: it is important the memory is zeroed out (by calling zero()) at the start of every
        // constructor. Much code relies on every byte being predictably initialized to zero.

        // This is a "missing" Value
        ValueStorage() { vsd.zero(); vsd.set_type(EOO); }

        explicit ValueStorage(BSONType t)                  { vsd.zero(); vsd.set_type(t); }
        ValueStorage(BSONType t, int i)                    { vsd.zero(); vsd.set_type(t); vsd.set_intValue(i); }
        ValueStorage(BSONType t, long long l)              { vsd.zero(); vsd.set_type(t); vsd.set_longValue(l); }
        ValueStorage(BSONType t, double d)                 { vsd.zero(); vsd.set_type(t); vsd.set_doubleValue(d); }
        ValueStorage(BSONType t, ReplTime r)               { vsd.zero(); vsd.set_type(t); vsd.set_timestampValue(r); }
        ValueStorage(BSONType t, bool b)                   { vsd.zero(); vsd.set_type(t); vsd.set_boolValue(b); }
        ValueStorage(BSONType t, const Document& d)        { vsd.zero(); vsd.set_type(t); putDocument(d); }
        ValueStorage(BSONType t, const RCVector* a)        { vsd.zero(); vsd.set_type(t); putVector(a); }
        ValueStorage(BSONType t, const StringData& s)      { vsd.zero(); vsd.set_type(t); putString(s); }
        ValueStorage(BSONType t, const BSONBinData& bd)    { vsd.zero(); vsd.set_type(t); putBinData(bd); }
        ValueStorage(BSONType t, const BSONRegEx& re)      { vsd.zero(); vsd.set_type(t); putRegEx(re); }
        ValueStorage(BSONType t, const BSONCodeWScope& cs) { vsd.zero(); vsd.set_type(t); putCodeWScope(cs); }
        ValueStorage(BSONType t, const BSONDBRef& dbref)   { vsd.zero(); vsd.set_type(t); putDBRef(dbref); }

        ValueStorage(BSONType t, const OID& o) {
            vsd.zero();
            vsd.set_type(t);
            memcpy(vsd.ptr_to_oid(), &o, sizeof(OID));
            BOOST_STATIC_ASSERT(sizeof(OID) == vsd.size_of_oid());
        }

        ValueStorage(const ValueStorage& rhs) {
            memcpy(vsd.base(), rhs.vsd.base(), vsd.size());
            memcpyed();
        }

        ~ValueStorage() {
            DEV verifyRefCountingIfShould();
            if (vsd.get_refCounter())
                intrusive_ptr_release(vsd.get_genericRCPtr());
            DEV memset(vsd.base(), 0xee, vsd.size());
        }

        ValueStorage& operator= (ValueStorage rhsCopy) {
            this->swap(rhsCopy);
            return *this;
        }

        void swap(ValueStorage& rhs) {
            // Don't need to update ref-counts because they will be the same in the end
            char temp[sizeof(ValueStorage)];
            memcpy(temp, this, sizeof(*this));
            memcpy(this, &rhs, sizeof(*this));
            memcpy(&rhs, temp, sizeof(*this));
        }

        /// Call this after memcpying to update ref counts if needed
        void memcpyed() const {
            DEV verifyRefCountingIfShould();
            if (vsd.get_refCounter())
                intrusive_ptr_add_ref(vsd.get_genericRCPtr());
        }

        /// These are only to be called during Value construction on an empty Value
        void putString(const StringData& s);
        void putVector(const RCVector* v);
        void putDocument(const Document& d);
        void putRegEx(const BSONRegEx& re);
        void putBinData(const BSONBinData& bd) {
            putRefCountable(
                RCString::create(
                    StringData(static_cast<const char*>(bd.data), bd.length)));
            vsd.set_binSubType(bd.type);
        }

        void putDBRef(const BSONDBRef& dbref) {
            putRefCountable(new RCDBRef(dbref.ns.toString(), dbref.oid));
        }

        void putCodeWScope(const BSONCodeWScope& cws) {
            putRefCountable(new RCCodeWScope(cws.code.toString(), cws.scope));
        }

        void putRefCountable(intrusive_ptr<const RefCountable> ptr) {
            vsd.set_genericRCPtr(ptr.get());

            if (vsd.get_genericRCPtr()) {
                intrusive_ptr_add_ref(vsd.get_genericRCPtr());
                vsd.set_refCounter(true);
            }
            DEV verifyRefCountingIfShould();
        }

        StringData getString() const {
            if (vsd.get_shortStr()) {
                return StringData(vsd.get_shortStrStorage(), vsd.get_shortStrSize());
            }
            else {
                dassert(typeid(*vsd.get_genericRCPtr()) == typeid(const RCString));
                const RCString* stringPtr = static_cast<const RCString*>(vsd.get_genericRCPtr());
                return StringData(stringPtr->c_str(), stringPtr->size());
            }
        }

        const vector<Value>& getArray() const {
            dassert(typeid(*genericRCPtr) == typeid(const RCVector));
            const RCVector* arrayPtr = static_cast<const RCVector*>(genericRCPtr);
            return arrayPtr->vec;
        }

        intrusive_ptr<const RCCodeWScope> getCodeWScope() const {
            dassert(typeid(*genericRCPtr) == typeid(const RCCodeWScope));
            return static_cast<const RCCodeWScope*>(genericRCPtr);
        }

        intrusive_ptr<const RCDBRef> getDBRef() const {
            dassert(typeid(*genericRCPtr) == typeid(const RCDBRef));
            return static_cast<const RCDBRef*>(genericRCPtr);
        }

        // Document is incomplete here so this can't be inline
        Document getDocument() const;

        BSONType bsonType() const {
            return BSONType(type);
        }

        BinDataType binDataType() const {
            dassert(type == BinData);
            return BinDataType(binSubType);
        }

        void zero() {
            memset(this, 0, sizeof(*this));
        }

        // Byte-for-byte identical
        bool identical(const ValueStorage& other) const {
            return  (i64[0] == other.i64[0]
                  && i64[1] == other.i64[1]);
        }

        void verifyRefCountingIfShould() const;
    };
    BOOST_STATIC_ASSERT(sizeof(ValueStorage) == 16);

}
