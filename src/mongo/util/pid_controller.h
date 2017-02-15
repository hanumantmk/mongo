/*    Copyright 2017 MongoDB Inc.
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

#include "mongo/util/time_support.h"

#include <iostream>

namespace mongo {

class PIDController {
public:
    PIDController(double max,
                  double min,
                  double proportionalConstant,
                  double derivativeConstant,
                  double integralConstant)
        : _max(max),
          _min(min),
          _proportionalConstant(proportionalConstant),
          _derivativeConstant(derivativeConstant),
          _integralConstant(integralConstant) {}

    double calculate(double setPoint, double value, Date_t now) {
        double error = setPoint - value;

        if (now == _lastTime) {
            return 0;
        }

        double deltaTime = 1;

        if (_lastTime.toULL()) {
            double dt = duration_cast<Milliseconds>(now - _lastTime).count();

            if (dt) {
                deltaTime = dt;
            }
        }

        _integral += error * deltaTime;

        double proportionalOutput = _proportionalConstant * error;

        double integralOutput = _integralConstant * _integral;

        double derivative = (error - _lastError) / deltaTime;
        double derivativeOutput = _derivativeConstant * derivative;

        double output = proportionalOutput + integralOutput + derivativeOutput;

//        if (output > _max) {
//            output = _max;
//        }
//
//        if (output < _min) {
//            output = _min;
//        }

        _lastError = error;
        _lastTime = now;

        return output;
    }

private:
    double _max;
    double _min;
    double _proportionalConstant;
    double _derivativeConstant;
    double _integralConstant;
    double _lastError = 0;
    double _integral = 0;
    Date_t _lastTime;
};
}
