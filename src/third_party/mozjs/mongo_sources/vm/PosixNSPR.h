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

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_PosixNSPR_h
#define vm_PosixNSPR_h

#include <stdint.h>

namespace nspr {
class Thread;
class Lock;
class CondVar;
};

typedef nspr::Thread PRThread;
typedef nspr::Lock PRLock;
typedef nspr::CondVar PRCondVar;

enum PRThreadType {
   PR_USER_THREAD,
   PR_SYSTEM_THREAD
};

enum PRThreadPriority
{
   PR_PRIORITY_FIRST   = 0,
   PR_PRIORITY_LOW     = 0,
   PR_PRIORITY_NORMAL  = 1,
   PR_PRIORITY_HIGH    = 2,
   PR_PRIORITY_URGENT  = 3,
   PR_PRIORITY_LAST    = 3
};

enum PRThreadScope {
   PR_LOCAL_THREAD,
   PR_GLOBAL_THREAD,
   PR_GLOBAL_BOUND_THREAD
};

enum PRThreadState {
   PR_JOINABLE_THREAD,
   PR_UNJOINABLE_THREAD
};

PRThread*
PR_CreateThread(PRThreadType type,
                void (*start)(void* arg),
                void* arg,
                PRThreadPriority priority,
                PRThreadScope scope,
                PRThreadState state,
                uint32_t stackSize);

typedef enum { PR_FAILURE = -1, PR_SUCCESS = 0 } PRStatus;

PRStatus
PR_JoinThread(PRThread* thread);

PRThread*
PR_GetCurrentThread();

PRStatus
PR_SetCurrentThreadName(const char* name);

typedef void (*PRThreadPrivateDTOR)(void* priv);

PRStatus
PR_NewThreadPrivateIndex(unsigned* newIndex, PRThreadPrivateDTOR destructor);

PRStatus
PR_SetThreadPrivate(unsigned index, void* priv);

void*
PR_GetThreadPrivate(unsigned index);

struct PRCallOnceType {
    int initialized;
    int32_t inProgress;
    PRStatus status;
};

typedef PRStatus (*PRCallOnceFN)();

PRStatus
PR_CallOnce(PRCallOnceType* once, PRCallOnceFN func);

typedef PRStatus (*PRCallOnceWithArgFN)(void*);

PRStatus
PR_CallOnceWithArg(PRCallOnceType* once, PRCallOnceWithArgFN func, void* arg);

PRLock*
PR_NewLock();

void
PR_DestroyLock(PRLock* lock);

void
PR_Lock(PRLock* lock);

PRStatus
PR_Unlock(PRLock* lock);

PRCondVar*
PR_NewCondVar(PRLock* lock);

void
PR_DestroyCondVar(PRCondVar* cvar);

PRStatus
PR_NotifyCondVar(PRCondVar* cvar);

PRStatus
PR_NotifyAllCondVar(PRCondVar* cvar);

#define PR_INTERVAL_MIN 1000UL
#define PR_INTERVAL_MAX 100000UL

#define PR_INTERVAL_NO_WAIT 0UL
#define PR_INTERVAL_NO_TIMEOUT 0xffffffffUL

uint32_t
PR_MillisecondsToInterval(uint32_t milli);

uint32_t
PR_MicrosecondsToInterval(uint32_t micro);

uint32_t
PR_TicksPerSecond();

PRStatus
PR_WaitCondVar(PRCondVar* cvar, uint32_t timeout);

#endif /* vm_PosixNSPR_h */
