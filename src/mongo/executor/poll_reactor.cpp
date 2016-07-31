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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kASIO

#include "mongo/platform/basic.h"

#include "mongo/db/service_context.h"
#include "mongo/executor/poll_reactor.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace executor {

constexpr size_t kReadControlFd = 0;
constexpr size_t kWriteControlFd = 1;

struct PosixWriter : public AsyncStreamInterface {
public:
    PosixWriter(int fd) : fd(fd) {}

    void connect(asio::ip::tcp::resolver::iterator endpoints,
                 ConnectHandler&& connectHandler) override {}

    void write(asio::const_buffer buf, StreamHandler&& writeHandler) override {}

    void read(asio::mutable_buffer buf, StreamHandler&& readHandler) override {}

    void cancel() override {}

    bool isOpen() override {
        return true;
    }

    int nativeHandle() override {
        return fd;
    }

    void setReactor(PollReactor* reactor) override {}

    PollReactor* getReactor() const override {
        return nullptr;
    }

    StatusWith<size_t> syncRead(DataRange dr) override {
        ssize_t r;
        for (;;) {
            r = ::read(fd, const_cast<char*>(dr.data()), dr.length());
            if (r < 0) {
                int err = errno;

                if (err == EAGAIN || err == EINTR) {
                    continue;
                }

                return Status(ErrorCodes::BadValue, "read failed in asio poll executor");
            }

            break;
        }

        return r;
    }

    StatusWith<size_t> syncWrite(ConstDataRange cdr) override {
        ssize_t r;
        for (;;) {
            r = ::write(fd, cdr.data(), cdr.length());
            if (r < 0) {
                int err = errno;

                if (err == EAGAIN || err == EINTR) {
                    continue;
                }

                return Status(ErrorCodes::BadValue, "write failed in asio poll executor");
            }

            break;
        }

        return r;
    }

private:
    int fd;
};

PollReactor::PollReactor() {
    auto r = pipe(pipefd);
    invariant(r == 0);

    struct pollfd pfd = {0};
    pfd.fd = pipefd[kReadControlFd];
    pfd.events = POLLIN | POLLERR | POLLHUP;
    pfds.push_back(pfd);

    controlAsyncStreamInterface = stdx::make_unique<PosixWriter>(pipefd[kReadControlFd]);

    auto pair =
        ops.emplace(std::piecewise_construct,
                    std::forward_as_tuple(controlAsyncStreamInterface.get()),
                    std::forward_as_tuple(opForPfd.size(), controlAsyncStreamInterface.get()));

    pair.first->second.readOp.emplace(DataRange(controlByte, controlByte + 1), [](Status) {});

    opForPfd.push_back(&pair.first->second);
}

PollReactor::~PollReactor() {
    close(pipefd[0]);
    close(pipefd[1]);
}

template <typename Callback>
void PollReactor::ensureNoPoll(Callback&& cb) {
    stdx::unique_lock<stdx::mutex> lk(mutex);

    requests++;

    if (inPoll) {
        char byte = 1;
        for (;;) {
            lk.unlock();
            auto r = ::write(pipefd[kWriteControlFd], &byte, 1);
            lk.lock();

            if (r < 0) {
                int err = errno;

                if (err == EAGAIN || err == EINTR) {
                    continue;
                }
            }

            invariant(r == 1);
            break;
        }

        condvar.wait(lk, [&] { return !inPoll; });
    }

    cb();

    requests--;

    condvar.notify_one();
}

void PollReactor::asyncRead(AsyncStreamInterface* stream,
                            DataRange dr,
                            stdx::function<void(Status)> cb) {
    ensureNoPoll([&] {
        auto iter = ops.find(stream);

        if (iter == ops.end()) {
            struct pollfd pfd = {0};
            pfd.fd = stream->nativeHandle();
            pfd.events = POLLERR | POLLHUP;
            pfds.push_back(pfd);

            std::tie(iter, std::ignore) =
                ops.emplace(std::piecewise_construct,
                            std::forward_as_tuple(stream),
                            std::forward_as_tuple(opForPfd.size(), stream));

            opForPfd.push_back(&iter->second);
        }

        invariant(!iter->second.readOp);
        iter->second.readOp.emplace(dr, std::move(cb));
        pfds[iter->second.idx].events |= POLLIN;
    });
}

void PollReactor::asyncWrite(AsyncStreamInterface* stream,
                             ConstDataRange dr,
                             stdx::function<void(Status)> cb) {
    ensureNoPoll([&] {
        auto iter = ops.find(stream);

        if (iter == ops.end()) {
            struct pollfd pfd = {0};
            pfd.fd = stream->nativeHandle();
            pfd.events = POLLERR | POLLHUP;
            pfds.push_back(pfd);

            std::tie(iter, std::ignore) =
                ops.emplace(std::piecewise_construct,
                            std::forward_as_tuple(stream),
                            std::forward_as_tuple(opForPfd.size(), stream));

            opForPfd.push_back(&iter->second);
        }

        invariant(!iter->second.writeOp);
        iter->second.writeOp.emplace(dr, std::move(cb));
        pfds[iter->second.idx].events |= POLLOUT;
    });
}

void PollReactor::cancel(AsyncStreamInterface* stream) {
    ensureNoPoll([&] {
        auto iter = ops.find(stream);

        if (iter != ops.end()) {
            removeIdx(iter->second.idx);
        }
    });
}

size_t PollReactor::setTimer(Date_t expiration, stdx::function<void()> callback) {
    size_t id;

    ensureNoPoll([&] {
        id = timerCounter++;
        activeTimers.emplace(id);
        timers.emplace(id, expiration, std::move(callback));
    });

    return id;
}

void PollReactor::cancelTimer(size_t id) {
    ensureNoPoll([&] { activeTimers.erase(id); });
}

void PollReactor::removeIdx(size_t idx) {
    auto endIdx = pfds.size() - 1;

    if (idx != endIdx) {
        using std::swap;

        swap(pfds[idx], pfds[endIdx]);
        swap(opForPfd[idx]->idx, opForPfd[endIdx]->idx);
        swap(opForPfd[idx], opForPfd[endIdx]);
    }

    ops.erase(opForPfd[endIdx]->stream);

    pfds.pop_back();
    opForPfd.pop_back();

    invariant(pfds.size());
}

bool PollReactor::empty() {
    stdx::lock_guard<stdx::mutex> lk(mutex);

    return ops.size() == 1 && activeTimers.empty();
}

void PollReactor::run() {
    std::vector<std::pair<Status, stdx::function<void(Status)>>> replies;
    std::vector<stdx::function<void()>> expiredTimers;

    auto expireTimers = [&] {
        auto now = getGlobalServiceContext()->getFastClockSource()->now();
        while (timers.size()) {
            auto& timer = timers.top();

            auto iter = activeTimers.find(timer.id);

            if (iter != activeTimers.end()) {
                if (timer.expiration < now) {
                    expiredTimers.push_back(std::move(timer.callback));
                } else {
                    break;
                }

                activeTimers.erase(iter);
            }

            timers.pop();
        }
    };

    auto cleanup = MakeGuard([&] {
        for (auto&& reply : replies) {
            reply.second(reply.first);
        }

        for (auto&& timer : expiredTimers) {
            timer();
        }

        condvar.notify_one();
    });

    stdx::unique_lock<stdx::mutex> lk(mutex);

    int r;

    for (;;) {
        condvar.wait(lk, [&] {
            expireTimers();
            return requests == 0 && (pfds.size() > 1 || timers.size());
        });

        Milliseconds timeout(-1);

        if (timers.size()) {
            timeout =
                timers.top().expiration - getGlobalServiceContext()->getFastClockSource()->now();
        }

        inPoll = true;
        lk.unlock();
        r = poll(pfds.data(), pfds.size(), timeout.count());
        lk.lock();
        inPoll = false;

        if (r < 0) {
            int err = errno;

            invariant(err == EAGAIN || err == EINTR);
            continue;

            // TODO handle errors
            return;
        }

        break;
    }

    expireTimers();

    for (int i = pfds.size() - 1; i >= 0 && r; i--) {
        auto& pfd = pfds[i];
        auto& op = *opForPfd[i];

        if (pfd.revents & (pfd.events | POLLERR | POLLHUP)) {
            r--;

            Status status = pfd.revents & (POLLERR | POLLHUP)
                ? Status(ErrorCodes::BadValue, "poll failed in asio poll executor")
                : Status::OK();

            if (status.isOK() && op.readOp && pfd.revents | POLLIN) {
                auto swRead = op.stream->syncRead(op.readOp->buffer);

                if (!swRead.isOK()) {
                    status = swRead.getStatus();
                } else if (swRead.isOK() && pfd.fd != pipefd[kReadControlFd]) {
                    op.readOp->buffer.advance(swRead.getValue());
                    if (op.readOp->buffer.length() == 0) {
                        replies.emplace_back(status, std::move(op.readOp->callback));
                        op.readOp = boost::none;
                    }
                }
            }

            if (status.isOK() && op.writeOp && pfd.revents | POLLOUT) {
                auto swWrite = op.stream->syncWrite(op.writeOp->buffer);

                if (!swWrite.isOK()) {
                    status = swWrite.getStatus();
                } else if (swWrite.isOK()) {
                    op.writeOp->buffer.advance(swWrite.getValue());
                    if (op.writeOp->buffer.length() == 0) {
                        replies.emplace_back(status, std::move(op.writeOp->callback));
                        op.writeOp = boost::none;
                    }
                }
            }

            if (!status.isOK()) {
                if (op.readOp) {
                    replies.emplace_back(status, std::move(op.readOp->callback));
                    op.readOp = boost::none;
                }

                if (op.writeOp) {
                    replies.emplace_back(status, std::move(op.writeOp->callback));
                    op.writeOp = boost::none;
                }
            }

            pfd.events = (op.readOp ? POLLIN : 0) | (op.writeOp ? POLLOUT : 0) | POLLERR | POLLHUP;

            if (!(op.readOp || op.writeOp)) {
                removeIdx(i);
            }
        }
    }
}

}  // namespace executor
}  // namespace mongo
