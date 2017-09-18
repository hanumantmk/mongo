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
#pragma once

#include "mongo/base/disallow_copying.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/concurrency/fifo_semaphore.h"
#include "mongo/util/time_support.h"

namespace mongo {

class ScopedTicket;
class TicketHolderReleaser;

class TicketHolder {
    MONGO_DISALLOW_COPYING(TicketHolder);
    friend ScopedTicket;
    friend TicketHolderReleaser;

public:
    explicit TicketHolder(int num);

    bool tryAcquire() {
        return _fifoSem.try_lock();
    }

    void waitForTicket() {
        return _fifoSem.lock();
    }

    bool waitForTicketUntil(Date_t until) {
        return _fifoSem.try_lock_until(until.toSystemTimePoint());
    }

    void release() {
        return _fifoSem.unlock();
    }

    Status resize(int newSize);

    int available() const;

    int used() const;

    int outof() const;

private:
    FifoSemaphore _fifoSem;

    AtomicInt32 _outof;
    stdx::mutex _resizeMutex;
};

class ScopedTicket {
public:
    ScopedTicket(TicketHolder* holder) : _lk(holder->_fifoSem) {}

private:
    stdx::unique_lock<FifoSemaphore> _lk;
};

class TicketHolderReleaser {
public:
    TicketHolderReleaser() = default;

    explicit TicketHolderReleaser(TicketHolder* holder) : _lk(holder->_fifoSem, stdx::adopt_lock) {}

    bool hasTicket() const {
        return _lk.owns_lock();
    }

    void reset(TicketHolder* holder = NULL) {
        _lk = holder ? decltype(_lk){holder->_fifoSem, stdx::adopt_lock} : decltype(_lk){};
    }

private:
    stdx::unique_lock<FifoSemaphore> _lk;
};
}
