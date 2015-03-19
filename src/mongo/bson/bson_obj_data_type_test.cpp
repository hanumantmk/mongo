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

#include "mongo/base/data_type_range.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/data_range.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"

#include "mongo/unittest/unittest.h"

namespace mongo {

    TEST(DataView, ConstDataTypeRangeBSON) {
        char buf[1000] = { 0 };

        DataRangeCursor drc(buf, buf + sizeof(buf));

        {
            BSONObjBuilder b;
            b.append("a", 1);

            drc.writeNativeAndAdvance(b.obj());
        }
        {
            BSONObjBuilder b;
            b.append("b", 2);

            drc.writeNativeAndAdvance(b.obj());
        }
        {
            BSONObjBuilder b;
            b.append("c", 3);

            drc.writeNativeAndAdvance(b.obj());
        }

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));
        ConstDataTypeRange<BSONObj> cdtr(&cdrc);

        ASSERT_EQUALS(cdrc.length(), 1000u);
        auto x = cdtr.cbegin();
        ASSERT_EQUALS(cdrc.length(), 988u);

        ASSERT_EQUALS(x->getField("a").numberInt(), 1);
        x++;

        ASSERT_EQUALS(x->getField("b").numberInt(), 2);
        ++x;

        ASSERT_EQUALS(x->getField("c").numberInt(), 3);
        ++x;

        ASSERT(cdtr.cend() == x);

        ASSERT_EQUALS(cdrc.length(), 964u);
    }

} // namespace mongo
