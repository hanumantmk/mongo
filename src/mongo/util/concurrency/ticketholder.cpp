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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/util/concurrency/ticketholder.h"

#include <iostream>

#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

TicketHolder::TicketHolder(int num) : _fifoSem(num), _outof(num) {}

Status TicketHolder::resize(int newSize) {
    stdx::lock_guard<stdx::mutex> lk(_resizeMutex);

    if (newSize < 5)
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Minimum value for semaphore is 5; given " << newSize);

    if (newSize > SEM_VALUE_MAX)
        return Status(ErrorCodes::BadValue,
                      str::stream() << "Maximum value for semaphore is " << SEM_VALUE_MAX
                                    << "; given "
                                    << newSize);

    while (_outof.load() < newSize) {
        release();
        _outof.fetchAndAdd(1);
    }

    while (_outof.load() > newSize) {
        waitForTicket();
        _outof.subtractAndFetch(1);
    }

    invariant(_outof.load() == newSize);
    return Status::OK();
}

int TicketHolder::available() const {
    return _fifoSem.value();
}

int TicketHolder::used() const {
    return outof() - available();
}

int TicketHolder::outof() const {
    return _outof.load();
}

}  // namespace mongo
