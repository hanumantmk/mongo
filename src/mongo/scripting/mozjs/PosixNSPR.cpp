/**
 * Copyright (C) 2015 MongoDB Inc.
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

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mongo/platform/basic.h"

#include <js/Utility.h>
#include <vm/PosixNSPR.h>

#include "mongo/stdx/chrono.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/mutex.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/concurrency/thread_name.h"
#include "mongo/util/concurrency/threadlocal.h"

class nspr::Thread {
    mongo::stdx::thread thread_;
    void (*start)(void* arg);
    void* arg;
    bool joinable;

public:
    Thread(void (*start)(void* arg), void* arg, bool joinable)
        : start(start), arg(arg), joinable(joinable) {}

    static void* ThreadRoutine(void* arg);

    mongo::stdx::thread& thread() {
        return thread_;
    }
};

namespace mongo {
TSP_DECLARE(nspr::Thread, kCurrentThread)
TSP_DEFINE(nspr::Thread, kCurrentThread)
}

void* nspr::Thread::ThreadRoutine(void* arg) {
    Thread* self = static_cast<Thread*>(arg);
    mongo::kCurrentThread.reset(self);
    self->start(self->arg);
    if (!self->joinable)
        js_delete(self);
    return nullptr;
}

PRThread* PR_CreateThread(PRThreadType type,
                          void (*start)(void* arg),
                          void* arg,
                          PRThreadPriority priority,
                          PRThreadScope scope,
                          PRThreadState state,
                          uint32_t stackSize) {
    MOZ_ASSERT(type == PR_USER_THREAD);
    MOZ_ASSERT(priority == PR_PRIORITY_NORMAL);

    try {
        std::unique_ptr<nspr::Thread, decltype(&js_delete<nspr::Thread>)> t(
            js_new<nspr::Thread>(start, arg, state != PR_UNJOINABLE_THREAD),
            js_delete<nspr::Thread>);

        t->thread() = mongo::stdx::thread(&nspr::Thread::ThreadRoutine, t.get());

        // TODO, do we actually care about setting stack size?

        if (state == PR_UNJOINABLE_THREAD) {
            t->thread().detach();
        }

        return t.release();
    } catch (...) {
        return nullptr;
    }
}

PRStatus PR_JoinThread(PRThread* thread) {
    try {
        thread->thread().join();

        js_delete(thread);

        return PR_SUCCESS;
    } catch (...) {
        return PR_FAILURE;
    }
}

PRThread* PR_GetCurrentThread() {
    return mongo::kCurrentThread.get();
}

PRStatus PR_SetCurrentThreadName(const char* name) {
    mongo::setThreadName(name);

    return PR_SUCCESS;
}

static const size_t MaxTLSKeyCount = 32;
static size_t gTLSKeyCount;
namespace mongo {
using TLSArray = std::array<void*, MaxTLSKeyCount>;

TSP_DECLARE(TLSArray, gTLSArray)
TSP_DEFINE(TLSArray, gTLSArray)
}

PRStatus PR_NewThreadPrivateIndex(unsigned* newIndex, PRThreadPrivateDTOR destructor) {
    /*
     * We only call PR_NewThreadPrivateIndex from the main thread, so there's no
     * need to lock the table of TLS keys.
     */
    MOZ_ASSERT(gTLSKeyCount + 1 < MaxTLSKeyCount);

    *newIndex = gTLSKeyCount;
    gTLSKeyCount++;

    return PR_SUCCESS;
}

PRStatus PR_SetThreadPrivate(unsigned index, void* priv) {
    if (index >= gTLSKeyCount)
        return PR_FAILURE;

    (*(mongo::gTLSArray.get()))[index] = priv;

    return PR_SUCCESS;
}

void* PR_GetThreadPrivate(unsigned index) {
    if (index >= gTLSKeyCount)
        return nullptr;

    return (*(mongo::gTLSArray.get()))[index];
}

PRStatus PR_CallOnce(PRCallOnceType* once, PRCallOnceFN func) {
    MOZ_CRASH("PR_CallOnce unimplemented");
}

PRStatus PR_CallOnceWithArg(PRCallOnceType* once, PRCallOnceWithArgFN func, void* arg) {
    MOZ_CRASH("PR_CallOnceWithArg unimplemented");
}

class nspr::Lock {
    mongo::stdx::mutex mutex_;

public:
    Lock() {}
    mongo::stdx::mutex& mutex() {
        return mutex_;
    }
};

PRLock* PR_NewLock() {
    return js_new<nspr::Lock>();
}

void PR_DestroyLock(PRLock* lock) {
    js_delete(lock);
}

void PR_Lock(PRLock* lock) {
    lock->mutex().lock();
}

PRStatus PR_Unlock(PRLock* lock) {
    try {
        lock->mutex().unlock();

        return PR_SUCCESS;
    } catch (...) {
        return PR_FAILURE;
    }
}

class nspr::CondVar {
    mongo::stdx::condition_variable cond_;
    nspr::Lock* lock_;

public:
    CondVar(nspr::Lock* lock) : lock_(lock) {}
    mongo::stdx::condition_variable& cond() {
        return cond_;
    }
    nspr::Lock* lock() {
        return lock_;
    }
};

PRCondVar* PR_NewCondVar(PRLock* lock) {
    return js_new<nspr::CondVar>(lock);
}

void PR_DestroyCondVar(PRCondVar* cvar) {
    js_delete(cvar);
}

PRStatus PR_NotifyCondVar(PRCondVar* cvar) {
    cvar->cond().notify_one();

    return PR_SUCCESS;
}

PRStatus PR_NotifyAllCondVar(PRCondVar* cvar) {
    cvar->cond().notify_all();

    return PR_SUCCESS;
}

uint32_t PR_MillisecondsToInterval(uint32_t milli) {
    return milli;
}

uint32_t PR_MicrosecondsToInterval(uint32_t micro) {
    return (micro + 999) / 1000;
}

static const uint64_t TicksPerSecond = 1000;
static const uint64_t NanoSecondsInSeconds = 1000000000;
static const uint64_t MicroSecondsInSeconds = 1000000;

uint32_t PR_TicksPerSecond() {
    return TicksPerSecond;
}

PRStatus PR_WaitCondVar(PRCondVar* cvar, uint32_t timeout) {
    if (timeout == PR_INTERVAL_NO_TIMEOUT) {
        try {
            mongo::stdx::unique_lock<mongo::stdx::mutex> lk(cvar->lock()->mutex(),
                                                            mongo::stdx::adopt_lock_t());

            cvar->cond().wait(lk);
            lk.release();

            return PR_SUCCESS;
        } catch (...) {
            return PR_FAILURE;
        }
    } else {
        try {
            mongo::stdx::unique_lock<mongo::stdx::mutex> lk(cvar->lock()->mutex(),
                                                            mongo::stdx::adopt_lock_t());

            cvar->cond().wait_for(lk, mongo::stdx::chrono::microseconds(timeout));
            lk.release();

            return PR_SUCCESS;
        } catch (...) {
            return PR_FAILURE;
        }
    }
}
