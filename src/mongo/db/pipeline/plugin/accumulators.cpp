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

#include "mongo/db/pipeline/accumulator.h"
#include "mongo/db/pipeline/value.h"
#include "mongo/db/pipeline/document_source_group.h"

namespace mongo {

    class AccumulatorNegativeSum : public Accumulator {
    public:
        virtual void processInternal(const Value& input, bool merging);
        virtual Value getValue(bool toBeMerged) const;
        virtual const char* getOpName() const;
        virtual void reset();
        virtual ~AccumulatorNegativeSum() {}

        static intrusive_ptr<Accumulator> create();

    private:
        AccumulatorNegativeSum();

        BSONType totalType;
        long long longTotal;
        double doubleTotal;
    };


    void AccumulatorNegativeSum::processInternal(const Value& input, bool merging) {
        // do nothing with non numeric types
        if (!input.numeric())
            return;

        // upgrade to the widest type required to hold the result
        totalType = Value::getWidestNumeric(totalType, input.getType());

        if (totalType == NumberInt || totalType == NumberLong) {
            long long v = input.coerceToLong();
            longTotal -= v;
            doubleTotal -= v;
        }
        else if (totalType == NumberDouble) {
            double v = input.coerceToDouble();
            doubleTotal -= v;
        }
        else {
            // non numerics should have returned above so we should never get here
            verify(false);
        }
    }

    intrusive_ptr<Accumulator> AccumulatorNegativeSum::create() {
        return new AccumulatorNegativeSum();
    }

    Value AccumulatorNegativeSum::getValue(bool toBeMerged) const {
        if (toBeMerged) {
            if (totalType == NumberLong) {
                return Value(-longTotal);
            }
            else if (totalType == NumberDouble) {
                return Value(-doubleTotal);
            }
            else if (totalType == NumberInt) {
                return Value::createIntOrLong(-longTotal);
            }
            else {
                massert(-1, "$negativeSum resulted in a non-numeric type", false);
            }
        } else {
            if (totalType == NumberLong) {
                return Value(longTotal);
            }
            else if (totalType == NumberDouble) {
                return Value(doubleTotal);
            }
            else if (totalType == NumberInt) {
                return Value::createIntOrLong(longTotal);
            }
            else {
                massert(-1, "$negativeSum resulted in a non-numeric type", false);
            }
        }
    }

    AccumulatorNegativeSum::AccumulatorNegativeSum()
        : totalType(NumberInt)
        , longTotal(0)
        , doubleTotal(0)
    {
        // This is a fixed size Accumulator so we never need to update this
        _memUsageBytes = sizeof(*this);
    }

    void AccumulatorNegativeSum::reset() {
        totalType = NumberInt;
        longTotal = 0;
        doubleTotal = 0;
    }

    const char *AccumulatorNegativeSum::getOpName() const {
        return "$negativeSum";
    }
}

extern "C" void mongo_add_accumulators(void * in) {
    std::vector<struct mongo::GroupOpDesc>& val = *reinterpret_cast<std::vector<struct mongo::GroupOpDesc>*>(in);

    struct mongo::GroupOpDesc accumulators[] = {
        { "$negativeSum", mongo::AccumulatorNegativeSum::create },
    };

    for (std::size_t i = 0; i < sizeof(accumulators) / sizeof(accumulators[0]); i++) {
        val.push_back(accumulators[i]);
    }

    return;
}
