// oid.h

/*    Copyright 2009 10gen Inc.
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

#include <string>

#include "mongo/util/encoded_value.h"
#include "mongo/bson/util/misc.h"
#include "mongo/util/hex.h"

namespace mongo {

#include "mongo/bson/encoded_value_machine_and_pid.h"

    ENCODED_VALUE_WRAPPER_CLASS_BEGIN(MachineAndPidPrivate, MachineAndPidGenerated)

        ENCODED_VALUE_CONST_METHODS_BEGIN
        public:
            bool operator!=(const T_CReference& rhs) const;
        ENCODED_VALUE_CONST_METHODS_END

        ENCODED_VALUE_REFERENCE_METHODS_BEGIN
        ENCODED_VALUE_REFERENCE_METHODS_END

        ENCODED_VALUE_VALUE_METHODS_BEGIN
        ENCODED_VALUE_VALUE_METHODS_END

        ENCODED_VALUE_CONST_CONSTRUCTORS_DEFAULT
        ENCODED_VALUE_REFERENCE_CONSTRUCTORS_DEFAULT
        ENCODED_VALUE_VALUE_CONSTRUCTORS_DEFAULT

    ENCODED_VALUE_WRAPPER_CLASS_END

#include "mongo/bson/encoded_value_oid.h"

    /** Object ID type.
        BSON objects typically have an _id field for the object id.  This field should be the first
        member of the object when present.  class OID is a special type that is a 12 byte id which
        is likely to be unique to the system.  You may also use other types for _id's.
        When _id field is missing from a BSON object, on an insert the database may insert one
        automatically in certain circumstances.

        Warning: You must call OID::newState() after a fork().

        Typical contents of the BSON ObjectID is a 12-byte value consisting of a 4-byte timestamp (seconds since epoch),
        a 3-byte machine id, a 2-byte process id, and a 3-byte counter. Note that the timestamp and counter fields must
        be stored big endian unlike the rest of BSON. This is because they are compared byte-by-byte and we want to ensure
        a mostly increasing order.
    */
    ENCODED_VALUE_WRAPPER_CLASS_BEGIN(OIDPrivate, OIDGenerated)

        enum {
            kOIDSize = 12,
            kIncSize = 3
        };

        typedef MachineAndPidPrivate<convertEndian> MachineAndPid;

        friend class MachineAndPidPrivate<convertEndian>;


        ENCODED_VALUE_CONST_METHODS_BEGIN

        public:
            const unsigned char *getData() const { return (const unsigned char *)this->data().ptr(); }

            bool operator==(const T_CReference& r) const { return this->a()==r.a() && this->b()==r.b(); }
            bool operator!=(const T_CReference& r) const { return this->a()!=r.a() || this->b()!=r.b(); }
            int compare( const T_CReference& other ) const { return memcmp( this->data().ptr() , other.data().ptr() , kOIDSize ); }
            bool operator<( const T_CReference& other ) const { return this->compare( other ) < 0; }
            bool operator<=( const T_CReference& other ) const { return this->compare( other ) <= 0; }

            /** @return the object ID output as 24 hex digits */
            std::string str() const { return toHexLower(this->data().ptr(), kOIDSize); }
            std::string toString() const { return this->str(); }
            /** @return the random/sequential part of the object ID as 6 hex digits */
            std::string toIncString() const { return toHexLower(this->_inc().ptr(), kIncSize); }

            /**
             * this is not consistent
             * do not store on disk
             */
            void hash_combine(std::size_t seed) const;

            time_t asTimeT() const;
            Date_t asDateT() const { return this->asTimeT() * (long long)1000; }

            bool isSet() const { return this->a() || this->b(); }

        ENCODED_VALUE_CONST_METHODS_END

        ENCODED_VALUE_REFERENCE_METHODS_BEGIN

        public:
            /** initialize to 'null' */
            void clear() { this->a() = 0; this->b() = 0; }


        ENCODED_VALUE_REFERENCE_METHODS_END

        ENCODED_VALUE_VALUE_METHODS_BEGIN
        public:
            /** sets the contents to a new oid / randomized value */
            void init();

            /** sets the contents to a new oid
             * guaranteed to be sequential
             * NOT guaranteed to be globally unique
             *     only unique for this process
             * */
            void initSequential();

            /** init from a 24 char hex std::string */
            void init( const std::string& s );

            /** Set to the min/max OID that could be generated at given timestamp. */
            void init( Date_t date, bool max=false );


        ENCODED_VALUE_VALUE_METHODS_END

        ENCODED_VALUE_CONST_CONSTRUCTORS_DEFAULT
        ENCODED_VALUE_REFERENCE_CONSTRUCTORS_DEFAULT

        ENCODED_VALUE_VALUE_CONSTRUCTORS_BEGIN {
        public:
            Value() {
                this->a() = 0;
                this->b() = 0;
            }

            Value(const char * _ptr) {
                memcpy(this->storage, _ptr, this->size);
            }

            Value& operator=(const Value& rhs) {
                memcpy(this->storage, rhs.ptr(), this->size);
                return *this;
            }

            /** init from a 24 char hex std::string */
            explicit Value(const std::string &s) { this->init(s); }

            /** init from a reference to a 12-byte array */
            explicit Value(const unsigned char (&arr)[kOIDSize]) {
                memcpy(this->data().ptr(), arr, sizeof(arr));
            }

            static Value gen() { Value o; o.init(); return o; }

            static unsigned getMachineId(); // features command uses
            static void regenMachineId(); // used by unit tests

        };

    private:
        /** call this after a fork to update the process id */
        static void justForked();

        static typename MachineAndPid::Value ourMachine, ourMachineAndPid;

        template <class T>
        static void foldInPid(typename MachineAndPidPrivate<convertEndian>::template T_Reference<T>& x);
        static typename MachineAndPid::Value genMachineAndPid();

    ENCODED_VALUE_WRAPPER_CLASS_END

    typedef OIDPrivate<>::Value OID;

    template <class T>
    std::ostream& operator<<( std::ostream &s, const typename OID::template T_CReference<T>& o );

    template <class T>
    inline StringBuilder& operator<< (StringBuilder& s, const typename OID::template T_CReference<T>& o) { return (s << o.str()); }

    /** Formatting mode for generating JSON from BSON.
        See <http://dochub.mongodb.org/core/mongodbextendedjson>
        for details.
    */
    enum JsonStringFormat {
        /** strict RFC format */
        Strict,
        /** 10gen format, which is close to JS format.  This form is understandable by
            javascript running inside the Mongo server via eval() */
        TenGen,
        /** Javascript JSON compatible */
        JS
    };

}
