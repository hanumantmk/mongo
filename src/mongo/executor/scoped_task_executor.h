/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <memory>

#include "mongo/base/status.h"
#include "mongo/executor/task_executor.h"
#include "mongo/stdx/mutex.h"
#include "mongo/stdx/unordered_set.h"
#include "mongo/util/if_constexpr.h"

namespace mongo {

class OperationContext;

namespace executor {

/**
 * Implements a scoped task executor which collects all callback handles it receives as part of
 * running operations and cancels any outstanding ones on destruction.
 *
 * The intent is that you can use this type with arbitrary task executor taking functions and allow
 * this objects destruction to do any clean up your type might need.
 */
class ScopedTaskExecutor {
    class Impl;

public:
    explicit ScopedTaskExecutor(TaskExecutor* executor)
        : _executor(std::make_shared<Impl>(executor)) {}

    ScopedTaskExecutor(const TaskExecutor&) = delete;
    ScopedTaskExecutor& operator=(const TaskExecutor&) = delete;

    ScopedTaskExecutor(TaskExecutor&&) = delete;
    ScopedTaskExecutor& operator=(TaskExecutor&&) = delete;

    ~ScopedTaskExecutor() {
        _executor->shutdown();
    }

    TaskExecutor* get() const {
        return _executor.get();
    }

    TaskExecutor* operator->() const {
        return _executor.get();
    }

    void shutdown() {
        _executor->shutdown();
    }

private:
    class Impl : public std::enable_shared_from_this<Impl>, public TaskExecutor {

        static const inline auto kShutdownStatus =
            Status(ErrorCodes::ShutdownInProgress, "Shutting down ScopedTaskExecutor::Impl");

    public:
        explicit Impl(TaskExecutor* executor) : _executor(executor) {}

        ~Impl() {
            invariant(_isShutdown);
        }

        void startup() override {
            MONGO_UNREACHABLE;
        }

        void shutdown() override {
            auto handles = [&] {
                stdx::lock_guard lk(_mutex);
                _isShutdown = true;

                return std::exchange(_cbHandles, {});
            }();

            for (auto&& handle : handles) {
                _executor->cancel(handle);
            }
        }

        void join() override {
            MONGO_UNREACHABLE;
        }

        void appendDiagnosticBSON(BSONObjBuilder* b) const override {
            _executor->appendDiagnosticBSON(b);
        }

        Date_t now() override {
            return _executor->now();
        }

        StatusWith<EventHandle> makeEvent() override {
            if (stdx::lock_guard lk(_mutex); _isShutdown) {
                return kShutdownStatus;
            }

            return _executor->makeEvent();
        }

        void signalEvent(const EventHandle& event) override {
            return _executor->signalEvent(event);
        }

        StatusWith<CallbackHandle> onEvent(const EventHandle& event, CallbackFn work) override {
            return _wrapCallback([&](auto&& x) { return _executor->scheduleWork(std::move(x)); },
                                 std::move(work));
        }

        void waitForEvent(const EventHandle& event) override {
            return _executor->waitForEvent(event);
        }

        StatusWith<stdx::cv_status> waitForEvent(OperationContext* opCtx,
                                                 const EventHandle& event,
                                                 Date_t deadline = Date_t::max()) override {
            return _executor->waitForEvent(opCtx, event, deadline);
        }

        StatusWith<CallbackHandle> scheduleWork(CallbackFn work) override {
            return _wrapCallback([&](auto&& x) { return _executor->scheduleWork(std::move(x)); },
                                 std::move(work));
        }

        StatusWith<CallbackHandle> scheduleWorkAt(Date_t when, CallbackFn work) override {
            return _wrapCallback(
                [&](auto&& x) { return _executor->scheduleWorkAt(when, std::move(x)); },
                std::move(work));
        }

        StatusWith<CallbackHandle> scheduleRemoteCommand(
            const RemoteCommandRequest& request,
            const RemoteCommandCallbackFn& cb,
            const BatonHandle& baton = nullptr) override {
            return _wrapCallback(
                [&](auto&& x) {
                    return _executor->scheduleRemoteCommand(request, std::move(x), baton);
                },
                cb);
        }

        void cancel(const CallbackHandle& cbHandle) override {
            return _executor->cancel(cbHandle);
        }

        void wait(const CallbackHandle& cbHandle,
                  Interruptible* interruptible = Interruptible::notInterruptible()) override {
            return _executor->wait(cbHandle, interruptible);
        }

        void appendConnectionStats(ConnectionPoolStats* stats) const override {
            return _executor->appendConnectionStats(stats);
        }

    private:
        /**
         * Wraps a scheduling call, along with its callback, so that:
         *
         * 1. The callback is invoked with a not-okay argument if this task executor has been
         *    shutdown.
         * 2. The callback handle that is returned from the call to schedule is collected and
         *    canceled, if this object is destroyed before the callback is invoked.
         */
        template <typename ScheduleCall, typename Work>
        StatusWith<CallbackHandle> _wrapCallback(ScheduleCall&& schedule, Work&& work) {
            if (stdx::lock_guard lk(_mutex); _isShutdown) {
                return kShutdownStatus;
            }

            auto swCbHandle = std::forward<ScheduleCall>(schedule)(
                [ this, work = std::forward<Work>(work), anchor = shared_from_this() ](
                    const auto& cargs) {
                    if (stdx::unique_lock lk(_mutex); _isShutdown) {
                        // Have to copy args because we get the arguments by const& and need to
                        // modify the status field.
                        auto args = cargs;

                        IF_CONSTEXPR(std::is_same_v<std::decay_t<decltype(args)>, CallbackArgs>) {
                            args.status = kShutdownStatus;
                        }
                        else {
                            args.response.status = kShutdownStatus;
                        }

                        lk.unlock();

                        return work(args);
                    } else {
                        // if we were shutdown already, all the handles have already been cancelled
                        // and dumped
                        _cbHandles.erase(cargs.myHandle);
                    }

                    work(cargs);
                });

            if (!swCbHandle.isOK()) {
                return swCbHandle;
            }

            if (stdx::unique_lock lk(_mutex); _isShutdown) {
                lk.unlock();
                _executor->cancel(swCbHandle.getValue());
            } else {
                _cbHandles.emplace(swCbHandle.getValue());
            }

            return swCbHandle;
        }

        stdx::mutex _mutex;

        bool _isShutdown = false;

        TaskExecutor* _executor;

        stdx::unordered_set<CallbackHandle> _cbHandles;
    };

    std::shared_ptr<Impl> _executor;
};

}  // namespace executor
}  // namespace mongo
