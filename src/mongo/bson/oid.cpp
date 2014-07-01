// @file oid.cpp

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

#include "mongo/pch.h"

#include <boost/functional/hash.hpp>

#include "mongo/platform/atomic_word.h"
#include "mongo/platform/process_id.h"
#include "mongo/platform/random.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/oid.h"

#define verify MONGO_verify

BOOST_STATIC_ASSERT( mongo::OIDPrivate<>::size == mongo::OIDPrivate<>::kOIDSize );
BOOST_STATIC_ASSERT( mongo::OIDPrivate<>::size == 12 );

namespace mongo {

    ENCODED_VALUE_CONST_METHOD(void, OIDPrivate)::hash_combine(size_t &seed) const {
        boost::hash_combine(seed, this->x());
        boost::hash_combine(seed, this->y());
        boost::hash_combine(seed, this->z());
    }

    // machine # before folding in the process id
    template<>
    OIDPrivate<>::MachineAndPid::Value OIDPrivate<>::ourMachine;

    ostream& operator<<( ostream &s, const OID &o ) {
        s << o.str();
        return s;
    }

    template<>
    template<class T>
    void OIDPrivate<>::foldInPid(typename MachineAndPidPrivate<>::T_Reference<T>& x) {
        unsigned p = ProcessId::getCurrent().asUInt32();
        x._pid() ^= static_cast<unsigned short>(p);
        // when the pid is greater than 16 bits, let the high bits modulate the machine id field.
        x._machineNumber() ^= p >> 16;
    }

    template<>
    MachineAndPidPrivate<>::Value OIDPrivate<>::genMachineAndPid() {
        BOOST_STATIC_ASSERT( mongo::OIDPrivate<>::MachineAndPid::size == 5 );

        // we only call this once per process
        scoped_ptr<SecureRandom> sr( SecureRandom::create() );
        int64_t _n;
        encoded_value::Reference<int64_t, encoded_value::endian::Little> n(reinterpret_cast<char *>(&_n));
        n = sr->nextInt64();
        memcpy(ourMachine.ptr(), n.ptr(), ourMachine.size);
        OIDPrivate<>::MachineAndPid::Value x = ourMachine;

        foldInPid(x);
        return x;
    }

    // after folding in the process id
    template<>
    OIDPrivate<>::MachineAndPid::Value OIDPrivate<>::ourMachineAndPid = OIDPrivate<>::genMachineAndPid();

    template<>
    void OIDPrivate<>::regenMachineId() {
        ourMachineAndPid = genMachineAndPid();
    }

    ENCODED_VALUE_CONST_METHOD(bool, MachineAndPidPrivate)::operator!=(const MachineAndPidPrivate<convertEndian>::T_CReference<T>& rhs) const {
        return this->_pid() != rhs._pid() || this->_machineNumber() != rhs._machineNumber();
    }

    template<>
    unsigned OIDPrivate<>::getMachineId() {
        return ourMachineAndPid._machineNumber();
    }

    template<>
    void OIDPrivate<>::justForked() {
        MachineAndPid::Value x = ourMachine;
        // we let the random # for machine go into all 5 bytes of MachineAndPid, and then
        // xor in the pid into _pid.  this reduces the probability of collisions.
        foldInPid(x);
        ourMachineAndPid = genMachineAndPid();
        verify( x != ourMachineAndPid );
        ourMachineAndPid = x;
    }

    ENCODED_VALUE_VALUE_METHOD(void, OIDPrivate)::init() {
        static AtomicUInt32 inc(
            static_cast<unsigned>(
                scoped_ptr<SecureRandom>(SecureRandom::create())->nextInt64()));

        {
            unsigned t = (unsigned) time(0);
            this->_time() = t;
        }

        this->_machineAndPid() = ourMachineAndPid.ptr();

        {
            int new_inc = inc.fetchAndAdd(1);
            this->_inc() = new_inc;
        }
    }

    static AtomicUInt64 _initSequential_sequence;

    ENCODED_VALUE_VALUE_METHOD(void, OIDPrivate)::initSequential() {

        {
            unsigned t = (unsigned) time(0);
            this->_time() = t;
        }
        
        {
            unsigned long long nextNumber = _initSequential_sequence.fetchAndAdd(1);
            unsigned char* numberData = reinterpret_cast<unsigned char*>(&nextNumber);
            for ( int i=0; i<8; i++ ) {
                this->data()[4+i] = numberData[7-i];
            }
        }
    }

    ENCODED_VALUE_VALUE_METHOD(void, OIDPrivate)::init( const std::string& s ) {
        verify( s.size() == 24 );
        const char *p = s.c_str();
        for( size_t i = 0; i < kOIDSize; i++ ) {
            this->data()[i] = fromHex(p);
            p += 2;
        }
    }

    ENCODED_VALUE_VALUE_METHOD(void, OIDPrivate)::init(Date_t date, bool max) {
        int time = (int) (date / 1000);
        this->_time() = time;

        if (max)
            encoded_value::Reference<long long>(this->data() + 4) = 0xFFFFFFFFFFFFFFFFll;
        else
            encoded_value::Reference<long long>(this->data() + 4) = 0x0000000000000000ll;
    }

    ENCODED_VALUE_CONST_METHOD(time_t, OIDPrivate)::asTimeT() const {
        return this->_time();
    }

    const string BSONObjBuilder::numStrs[] = {
        "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",
        "10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
        "20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
        "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
        "40", "41", "42", "43", "44", "45", "46", "47", "48", "49",
        "50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
        "60", "61", "62", "63", "64", "65", "66", "67", "68", "69",
        "70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
        "80", "81", "82", "83", "84", "85", "86", "87", "88", "89",
        "90", "91", "92", "93", "94", "95", "96", "97", "98", "99",
    };

    // This is to ensure that BSONObjBuilder doesn't try to use numStrs before the strings have been constructed
    // I've tested just making numStrs a char[][], but the overhead of constructing the strings each time was too high
    // numStrsReady will be 0 until after numStrs is initialized because it is a static variable
    bool BSONObjBuilder::numStrsReady = (numStrs[0].size() > 0);

}
