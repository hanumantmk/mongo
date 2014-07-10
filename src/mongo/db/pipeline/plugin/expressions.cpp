/**
 * Copyright (c) 2011 10gen Inc.
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

#include "mongo/pch.h"

#include "mongo/db/pipeline/expression.h"

#include <boost/algorithm/string.hpp>
#include <boost/preprocessor/cat.hpp> // like the ## operator but works with __LINE__
#include <cstdio>

#include "mongo/base/init.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/pipeline/document.h"
#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/value.h"
#include "mongo/stdx/functional.h"
#include "mongo/util/string_map.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    class ExpressionAddAndMul2 : public ExpressionVariadic<ExpressionAddAndMul2> {
    public:
        // virtuals from Expression
        virtual Value evaluateInternal(Variables* vars) const;
        virtual const char *getOpName() const;
        virtual bool isAssociativeAndCommutative() const { return true; }
        virtual ~ExpressionAddAndMul2() {}
    };

    Value ExpressionAddAndMul2::evaluateInternal(Variables* vars) const {

        /*
          We'll try to return the narrowest possible result value.  To do that
          without creating intermediate Values, do the arithmetic for double
          and integral types in parallel, tracking the current narrowest
          type.
         */
        double doubleTotal = 0;
        long long longTotal = 0;
        BSONType totalType = NumberInt;
        bool haveDate = false;

        const size_t n = vpOperand.size();
        for (size_t i = 0; i < n; ++i) {
            Value val = vpOperand[i]->evaluateInternal(vars);

            if (val.numeric()) {
                totalType = Value::getWidestNumeric(totalType, val.getType());

                doubleTotal += 2 * val.coerceToDouble();
                longTotal += 2 * val.coerceToLong();
            }
            else if (val.getType() == Date) {
                uassert(-1, "only one Date allowed in an $addAndMul2 expression",
                        !haveDate);
                haveDate = true;

                // We don't manipulate totalType here.

                longTotal += val.getDate();
                doubleTotal += val.getDate();
            }
            else if (val.nullish()) {
                return Value(BSONNULL);
            }
            else {
                uasserted(-1, str::stream() << "$addAndMul2 only supports numeric or date types, not "
                                               << typeName(val.getType()));
            }
        }

        if (haveDate) {
            if (totalType == NumberDouble)
                longTotal = static_cast<long long>(doubleTotal);
            return Value(Date_t(longTotal));
        }
        else if (totalType == NumberLong) {
            return Value(longTotal);
        }
        else if (totalType == NumberDouble) {
            return Value(doubleTotal);
        }
        else if (totalType == NumberInt) {
            return Value::createIntOrLong(longTotal);
        }
        else {
            massert(-1, "$addAndMul2 resulted in a non-numeric type", false);
        }
    }

    const char *ExpressionAddAndMul2::getOpName() const {
        return "$addAndMul2";
    }
}

extern "C" void mongo_add_expressions(void * in) {
    mongo::StringMap<mongo::ExpressionParser>& expressionParserMap = *reinterpret_cast<mongo::StringMap<mongo::ExpressionParser>*>(in);

    std::cout << "slurped some crap" << std::endl;

    expressionParserMap["$addAndMul2"] = mongo::ExpressionAddAndMul2::parse;

    return;
}
