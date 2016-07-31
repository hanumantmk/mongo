/**
 *    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <boost/optional.hpp>
#include <poll.h>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mongo/base/data_range_cursor.h"
#include "mongo/executor/async_stream_interface.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace executor {

class PollReactor {
public:
    PollReactor();

    ~PollReactor();

    void asyncRead(AsyncStreamInterface* stream, DataRange dr, stdx::function<void(Status)> cb);
    void asyncWrite(AsyncStreamInterface* stream,
                    ConstDataRange cdr,
                    stdx::function<void(Status)> cb);
    void cancel(AsyncStreamInterface* stream);

    size_t setTimer(Date_t expiration, stdx::function<void()> callback);
    void cancelTimer(size_t id);

    void run();

    bool empty();

private:
    struct Timer {
        Timer(size_t id, Date_t expiration, stdx::function<void()> cb)
            : id(id), expiration(expiration), callback(std::move(cb)) {}

        size_t id;
        Date_t expiration;
        stdx::function<void()> callback;
    };
    struct TimerComparator {
        bool operator()(const Timer& lhs, const Timer& rhs) {
            return lhs.expiration > rhs.expiration;
        }
    };

    struct ReadOp {
        ReadOp(DataRange dr, stdx::function<void(Status)> cb)
            : buffer(dr), callback(std::move(cb)) {}

        DataRangeCursor buffer;
        stdx::function<void(Status)> callback;
    };
    struct WriteOp {
        WriteOp(ConstDataRange dr, stdx::function<void(Status)> cb)
            : buffer(dr), callback(std::move(cb)) {}

        ConstDataRangeCursor buffer;
        stdx::function<void(Status)> callback;
    };
    struct Op {
        Op(size_t idx, AsyncStreamInterface* stream) : idx(idx), stream(stream) {}

        size_t idx;
        AsyncStreamInterface* stream;
        boost::optional<ReadOp> readOp;
        boost::optional<WriteOp> writeOp;
    };

    void removeIdx(size_t idx);

    template <typename Callback>
    void ensureNoPoll(Callback&&);

    stdx::mutex mutex;
    stdx::condition_variable condvar;
    int pipefd[2];
    char controlByte[2];
    std::unique_ptr<AsyncStreamInterface> controlAsyncStreamInterface;
    bool inPoll = false;
    size_t requests = 0;
    std::vector<struct pollfd> pfds;
    std::vector<Op*> opForPfd;
    std::unordered_map<AsyncStreamInterface*, Op> ops;
    size_t timerCounter = 0;
    std::unordered_set<size_t> activeTimers;
    std::priority_queue<Timer, std::vector<Timer>, TimerComparator> timers;
};


}  // namespace executor
}  // namespace mongo
