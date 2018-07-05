/**
 * Copyright (C) 2018 MongoDB Inc.
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
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/time_support.h"
#include "mongo/util/waitable.h"

namespace mongo {

/**
 * A type which can be used to wait on condition variables with a level triggered one-way interrupt.
 * I.e. after the interrupt is triggered (via some non-public api call) subsequent calls to
 * waitForConditionXXX will fail with a negative status.  Interrupts must unblock all callers of
 * waitForConditionXXX.
 */
class Interruptable {
protected:
    struct DeadlineState {
        Date_t deadline;
        bool hasArtificialDeadline;
    };

    struct IgnoreInterruptsState {
        bool ignoreInterrupts;
        DeadlineState deadline;
    };

    /**
     * Pushes an ignore interruption critical section into the interruptable.  Until an associated
     * popIgnoreInterrupts is invoked, the interruptable should ignore interruptions.
     *
     * Returns state needed to pop interruption.
     */
    virtual IgnoreInterruptsState _pushIgnoreInterrupts() = 0;

    /**
     * Pops the ignored interruption critical section introduced by push.
     */
    virtual void _popIgnoreInterrupts(IgnoreInterruptsState iis) = 0;

    /**
     * Pushes a subsidiary deadline into the interruptble.  Until an associated popFatalDeadline is
     * invoked, the interruptable will fail checkForInterrupt and waitForConditionOrInterrupt calls
     * with InternalExceededTimeLimit if the deadline has passed.
     *
     * Returns state needed to pop the deadline.
     */
    virtual DeadlineState _pushFatalDeadline(Date_t deadline) = 0;

    /**
     * Pops the subsidiary deadline introduced by push.
     */
    virtual void _popFatalDeadline(DeadlineState) = 0;

    /**
     * Returns the equivalent of Date_t::now() + waitFor for the interruptable's clock
     */
    virtual Date_t _getExpirationDateForWaitForValue(Milliseconds waitFor) = 0;

    class NoopInterruptable;

public:
    /**
     * Returns the Noop interruptable.  Useful as a default argument to interruptable taking
     * methods.
     */
    static Interruptable* Noop();

    /**
     * A deadline guard which when allocated on the stack provides a subsidiary deadline to the
     * parent.
     */
    class DeadlineGuard {
        friend Interruptable;

        explicit DeadlineGuard(Interruptable& interruptable, Date_t newDeadline)
            : _interruptable(interruptable),
              _oldDeadline(_interruptable._pushFatalDeadline(newDeadline)) {}

    public:
        ~DeadlineGuard() {
            _interruptable._popFatalDeadline(_oldDeadline);
        }

    private:
        Interruptable& _interruptable;
        DeadlineState _oldDeadline;
    };

    /**
     * makeDeadlineGuard introduces a new subsidiary scope in which, for the liftime of the returned
     * guard, the interruptable has a new fatal deadline.  All operations which check
     * checkForInterrupt or waitForConditionOrInterrupt will return InternalExceededTimeLimit if the
     * interruptable's now() is past the deadline.
     */
    DeadlineGuard makeDeadlineGuard(Date_t deadline) {
        return DeadlineGuard(*this, deadline);
    }

    /**
     * Invokes the passed callback with a deadline guard active initialized with the passed
     * deadline.  Additionally handles the dance of try/catching the invocation and checking
     * checkForInterrupt with the guard inactive (to allow a higher level timeout to override a
     * lower level one)
     */
    template <typename Callback>
    decltype(auto) runWithDeadline(Date_t deadline, Callback&& cb) {
        try {
            const auto guard = makeDeadlineGuard(deadline);
            return std::forward<Callback>(cb)();
        } catch (const ExceptionFor<ErrorCodes::InternalExceededTimeLimit>&) {
            checkForInterrupt();
            throw;
        }
    }

    /**
     * Returns true if this interruptable has a deadline.
     */
    bool hasDeadline() const {
        return getDeadline() < Date_t::max();
    }

    /**
     * Returns the deadline for this interruptable, or Date_t::max() if there is no deadline.
     */
    virtual Date_t getDeadline() const = 0;

    /**
     * An interruption guard which when allocated on the stack provides a region where interruption
     * is ignored.
     *
     * Note that this causes the deadline to be reset to Date_t::max(), but that it can also be
     * subsequently reduced in size after the fact.
     */
    class InterruptionGuard {
        friend Interruptable;

        explicit InterruptionGuard(Interruptable& interruptable)
            : _interruptable(interruptable), _oldState(_interruptable._pushIgnoreInterrupts()) {}

    public:
        ~InterruptionGuard() {
            _interruptable._popIgnoreInterrupts(_oldState);
        }

    private:
        Interruptable& _interruptable;
        IgnoreInterruptsState _oldState;
    };

    /**
     * makeDeadlineGuard introduces a new subsidiary scope in which, for the liftime of the returned
     * guard, the interruptable ignores outside interruption.  All operations which check
     * checkForInterrupt or waitForConditionOrInterrupt will return Status::OK, unless a new
     * deadline guard is established, in which case they may return InternalExceededTimeLimit.
     */
    InterruptionGuard makeInterruptionGuard() {
        return InterruptionGuard(*this);
    }

    /**
     * Invokes the passed callback with an interruption guard active.  Additionally handles the
     * dance of try/catching the invocation and checking checkForInterrupt with the guard inactive
     * (to allow a higher level timeout to override a lower level one, or for top level interruption
     * to propogate)
     */
    template <typename Callback>
    decltype(auto) runWithoutInterruption(Callback&& cb) {
        try {
            const auto guard = makeInterruptionGuard();
            return std::forward<Callback>(cb)();
        } catch (const ExceptionFor<ErrorCodes::InternalExceededTimeLimit>&) {
            checkForInterrupt();
            throw;
        }
    }

    /**
     * Raises a AssertionException if this operation is in a killed state.
     */
    void checkForInterrupt() {
        uassertStatusOK(checkForInterruptNoAssert());
    }

    /**
     * Returns Status::OK() unless this operation is in a killed state.
     */
    virtual Status checkForInterruptNoAssert() noexcept = 0;

    /**
     * Waits for either the condition "cv" to be signaled, this operation to be interrupted, or the
     * deadline on this operation to expire.  In the event of interruption or operation deadline
     * expiration, raises a AssertionException with an error code indicating the interruption type.
     */
    void waitForConditionOrInterrupt(stdx::condition_variable& cv,
                                     stdx::unique_lock<stdx::mutex>& m) {
        uassertStatusOK(waitForConditionOrInterruptNoAssert(cv, m));
    }

    /**
     * Waits on condition "cv" for "pred" until "pred" returns true, or this operation
     * is interrupted or its deadline expires. Throws a DBException for interruption and
     * deadline expiration.
     */
    template <typename Pred>
    void waitForConditionOrInterrupt(stdx::condition_variable& cv,
                                     stdx::unique_lock<stdx::mutex>& m,
                                     Pred pred) {
        while (!pred()) {
            waitForConditionOrInterrupt(cv, m);
        }
    }

    /**
     * Same as waitForConditionOrInterrupt, except returns a Status instead of throwing
     * a DBException to report interruption.
     */
    Status waitForConditionOrInterruptNoAssert(stdx::condition_variable& cv,
                                               stdx::unique_lock<stdx::mutex>& m) noexcept {
        auto status = waitForConditionOrInterruptNoAssertUntil(cv, m, Date_t::max());
        if (!status.isOK()) {
            return status.getStatus();
        }

        invariant(status.getValue() == stdx::cv_status::no_timeout);
        return Status::OK();
    }

    /**
     * Same as the predicate form of waitForConditionOrInterrupt, except that it returns a not okay
     * status instead of throwing on interruption.
     */
    template <typename Pred>
    Status waitForConditionOrInterruptNoAssert(stdx::condition_variable& cv,
                                               stdx::unique_lock<stdx::mutex>& m,
                                               Pred pred) noexcept {
        while (!pred()) {
            auto status = waitForConditionOrInterruptNoAssert(cv, m);

            if (!status.isOK()) {
                return status;
            }
        }

        return Status::OK();
    }

    /**
     * Waits for condition "cv" to be signaled, or for the given "deadline" to expire, or
     * for the operation to be interrupted, or for the operation's own deadline to expire.
     *
     * If the operation deadline expires or the operation is interrupted, throws a DBException.  If
     * the given "deadline" expires, returns cv_status::timeout. Otherwise, returns
     * cv_status::no_timeout.
     */
    stdx::cv_status waitForConditionOrInterruptUntil(stdx::condition_variable& cv,
                                                     stdx::unique_lock<stdx::mutex>& m,
                                                     Date_t deadline) {
        return uassertStatusOK(waitForConditionOrInterruptNoAssertUntil(cv, m, deadline));
    }

    /**
     * Waits on condition "cv" for "pred" until "pred" returns true, or the given "deadline"
     * expires, or this operation is interrupted, or this operation's own deadline expires.
     *
     *
     * If the operation deadline expires or the operation is interrupted, throws a DBException.  If
     * the given "deadline" expires, returns cv_status::timeout. Otherwise, returns
     * cv_status::no_timeout indicating that "pred" finally returned true.
     */
    template <typename Pred>
    bool waitForConditionOrInterruptUntil(stdx::condition_variable& cv,
                                          stdx::unique_lock<stdx::mutex>& m,
                                          Date_t deadline,
                                          Pred pred) {
        while (!pred()) {
            if (stdx::cv_status::timeout == waitForConditionOrInterruptUntil(cv, m, deadline)) {
                return pred();
            }
        }
        return true;
    }

    /**
     * Same as the non-predicate form of waitForConditionOrInterruptUntil, but takes a relative
     * amount of time to wait instead of an absolute time point.
     */
    stdx::cv_status waitForConditionOrInterruptFor(stdx::condition_variable& cv,
                                                   stdx::unique_lock<stdx::mutex>& m,
                                                   Milliseconds ms) {
        return uassertStatusOK(
            waitForConditionOrInterruptNoAssertUntil(cv, m, _getExpirationDateForWaitForValue(ms)));
    }

    /**
     * Same as the predicate form of waitForConditionOrInterruptUntil, but takes a relative
     * amount of time to wait instead of an absolute time point.
     */
    template <typename Pred>
    bool waitForConditionOrInterruptFor(stdx::condition_variable& cv,
                                        stdx::unique_lock<stdx::mutex>& m,
                                        Milliseconds ms,
                                        Pred pred) {
        while (!pred()) {
            if (stdx::cv_status::timeout == waitForConditionOrInterruptFor(cv, m, ms)) {
                return pred();
            }
        }
        return true;
    }

    /**
     * Same as waitForConditionOrInterruptUntil, except returns StatusWith<stdx::cv_status> and
     * non-ok status indicates the error instead of a DBException.
     */
    virtual StatusWith<stdx::cv_status> waitForConditionOrInterruptNoAssertUntil(
        stdx::condition_variable& cv,
        stdx::unique_lock<stdx::mutex>& m,
        Date_t deadline) noexcept = 0;

    /**
     * Sleeps until "deadline"; throws an exception if the interrutipble is interrupted before then.
     */
    void sleepUntil(Date_t deadline) {
        stdx::mutex m;
        stdx::condition_variable cv;
        stdx::unique_lock<stdx::mutex> lk(m);
        invariant(!waitForConditionOrInterruptUntil(cv, lk, deadline, [] { return false; }));
    }

    /**
     * Sleeps for "duration" ms; throws an exception if the interruptible is interrupted before
     * then.
     */
    void sleepFor(Milliseconds duration) {
        stdx::mutex m;
        stdx::condition_variable cv;
        stdx::unique_lock<stdx::mutex> lk(m);
        invariant(!waitForConditionOrInterruptFor(cv, lk, duration, [] { return false; }));
    }
};

/**
 * A noop interrutible type which can be used as a lightweight default arg for interruptible taking
 * functions.
 */
class Interruptable::NoopInterruptable : public Interruptable {
    StatusWith<stdx::cv_status> waitForConditionOrInterruptNoAssertUntil(
        stdx::condition_variable& cv,
        stdx::unique_lock<stdx::mutex>& m,
        Date_t deadline) noexcept override {

        if (deadline == Date_t::max()) {
            cv.wait(m);
            return stdx::cv_status::no_timeout;
        }

        return cv.wait_until(m, deadline.toSystemTimePoint());
    }

    Date_t getDeadline() const override {
        return Date_t::max();
    }

    Status checkForInterruptNoAssert() noexcept override {
        return Status::OK();
    }

    // It's invalid to call the deadline or ignore interruption guards on a possibly noop
    // interruptable.
    //
    // The noop interruptable should only be invoked as a default arg at the bottom of the call
    // stack (with types that won't modify it's invocation)
    IgnoreInterruptsState _pushIgnoreInterrupts() override {
        MONGO_UNREACHABLE;
    }

    void _popIgnoreInterrupts(IgnoreInterruptsState) override {
        MONGO_UNREACHABLE;
    }

    DeadlineState _pushFatalDeadline(Date_t deadline) override {
        MONGO_UNREACHABLE;
    }

    void _popFatalDeadline(DeadlineState) override {
        MONGO_UNREACHABLE;
    }

    Date_t _getExpirationDateForWaitForValue(Milliseconds waitFor) override {
        return Date_t::now() + waitFor;
    }
};

inline Interruptable* Interruptable::Noop() {
    static NoopInterruptable noop;

    return &noop;
}

}  // namespace mongo
