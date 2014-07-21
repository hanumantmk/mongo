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

#include "mongo/bson/util/misc.h"
#include "mongo/util/encoded_value.h"
#include "mongo/util/hex.h"

namespace mongo {

    EV_DECL_NS_BEGIN(MachineAndPid_EV)

#pragma pack(1)
    EV_DECL_STRUCT {
        unsigned char _machineNumber[3];
        unsigned short _pid;
    };
#pragma pack()

    EV_DECL_CONST_METHODS_BEGIN
    public:
        EV_DECL_RAW_ACCESSOR(_machineNumber)
        EV_DECL_ACCESSOR(_pid)

        bool operator!=(const cview& rhs) const;
    EV_DECL_CONST_METHODS_END

    EV_DECL_MUTABLE_METHODS_BEGIN
    public:
        EV_DECL_RAW_MUTATOR(_machineNumber)
        EV_DECL_MUTATOR(_pid)
    EV_DECL_MUTABLE_METHODS_END

    EV_DECL_NS_END

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
    EV_DECL_NS_BEGIN(OID_EV)
        enum {
            kOIDSize = 12,
            kIncSize = 3
        };

#pragma pack(1)
    EV_DECL_STRUCT {
        union {
            struct {
                // 12 bytes total
                unsigned char _time[4];
                MachineAndPid_EV::value _machineAndPid;
                unsigned char _inc[3];
            };
            struct {
                long long a;
                unsigned b;
            };
            struct {
                // TODO: get rid of this eventually
                //       this is a hack because of hash_combine with older versions of boost
                //       on 32-bit platforms
                int x;
                int y;
                int z;
            };
            unsigned char data[kOIDSize];
        };
    };
#pragma pack()

    EV_DECL_CONST_METHODS_BEGIN
    protected:
        EV_DECL_RAW_ACCESSOR(_time)
        EV_DECL_VIEW_ACCESSOR(_machineAndPid)
        EV_DECL_RAW_ACCESSOR(_inc)
        EV_DECL_ACCESSOR(a)
        EV_DECL_ACCESSOR(b)
        EV_DECL_ACCESSOR(x)
        EV_DECL_ACCESSOR(y)
        EV_DECL_ACCESSOR(z)
        EV_DECL_RAW_ACCESSOR(data)

    public:
        const unsigned char *getData() const { return reinterpret_cast<const unsigned char *>(data()); }

        template <class T>
        bool operator==(const const_methods<T>& r) const { return a()==r.a() && b()==r.b(); }

        template <class T>
        bool operator!=(const const_methods<T>& r) const { return a()!=r.a() || b()!=r.b(); }

        template <class T>
        int compare( const const_methods<T>& other ) const { return memcmp( data() , other.data() , kOIDSize ); }

        template <class T>
        bool operator<( const const_methods<T>& other ) const { return compare( other ) < 0; }

        template <class T>
        bool operator<=( const const_methods<T>& other ) const { return compare( other ) <= 0; }

        /** @return the object ID output as 24 hex digits */
        std::string str() const { return toHexLower(data(), kOIDSize); }
        std::string toString() const { return str(); }
        /** @return the random/sequential part of the object ID as 6 hex digits */
        std::string toIncString() const { return toHexLower(_inc(), kIncSize); }

        time_t asTimeT() const;
        Date_t asDateT() const { return asTimeT() * (long long)1000; }

        bool isSet() const { return a() || b(); }

        /**
         * this is not consistent
         * do not store on disk
         */
        void hash_combine(std::size_t &seed) const;

    EV_DECL_CONST_METHODS_END

    EV_DECL_MUTABLE_METHODS_BEGIN
    protected:
        EV_DECL_RAW_MUTATOR(_time)
        EV_DECL_VIEW_MUTATOR(_machineAndPid)
        EV_DECL_RAW_MUTATOR(_inc)
        EV_DECL_MUTATOR(a)
        EV_DECL_MUTATOR(b)
        EV_DECL_MUTATOR(x)
        EV_DECL_MUTATOR(y)
        EV_DECL_MUTATOR(z)
        EV_DECL_RAW_MUTATOR(data)

    public:

        /** initialize to 'null' */
        void clear() { a(0); b(0); }

    EV_DECL_MUTABLE_METHODS_END

    EV_DECL_NS_END

    class OID : public OID_EV::value {
    public:

        /**
         * Functor compatible with std::hash for std::unordered_{map,set}
         * Warning: The hash function is subject to change. Do not use in cases where hashes need
         *          to be consistent across versions.
         */
        struct Hasher {
            std::size_t operator() (const OID& oid) const;
        };

        OID() : OID_EV::value(0) { }

        /** init from a 24 char hex std::string */
        void init( const std::string& s );

        /** Set to the min/max OID that could be generated at given timestamp. */
        void init( Date_t date, bool max=false );

        /** init from a 24 char hex std::string */
        explicit OID(const std::string &s) { init(s); }

        /** init from a reference to a 12-byte array */
        explicit OID(const unsigned char (&arr)[OID_EV::kOIDSize]) {
            memcpy(data(), arr, sizeof(arr));
        }

        /** sets the contents to a new oid / randomized value */
        void init();

        static OID gen() { OID o; o.init(); return o; }

        /** sets the contents to a new oid
         * guaranteed to be sequential
         * NOT guaranteed to be globally unique
         *     only unique for this process
         * */
        void initSequential();

        /** call this after a fork to update the process id */
        static void justForked();

        static unsigned getMachineId(); // features command uses
        static void regenMachineId(); // used by unit tests

    private:
        typedef MachineAndPid_EV::value MachineAndPid;
        static MachineAndPid ourMachine, ourMachineAndPid;

        static void foldInPid(MachineAndPid& x);
        static MachineAndPid genMachineAndPid();
    };

    std::ostream& operator<<( std::ostream &s, const OID &o );
    inline StringBuilder& operator<< (StringBuilder& s, const OID& o) { return (s << o.str()); }

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
