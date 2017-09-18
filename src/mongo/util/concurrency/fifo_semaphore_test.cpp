/**
 * Copyright (C) 2017 MongoDB Inc.
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


#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include <array>

#include "mongo/unittest/unittest.h"

#include "mongo/util/concurrency/fifo_semaphore.h"

#include "mongo/platform/atomic_word.h"
#include "mongo/stdx/thread.h"

namespace {
using namespace mongo;

TEST(FifoSempahoreTest, Multi) {
    FifoSemaphore fifoSem(0);

    constexpr size_t nConcurrent = 100;
    std::array<stdx::thread, nConcurrent> threads;
    AtomicUInt32 finished(0);

    for (auto& thread : threads) {
        thread = stdx::thread([&] {
            stdx::lock_guard<FifoSemaphore> lk(fifoSem);
            finished.addAndFetch(1);
        });
    }

    while (fifoSem.waiters() < nConcurrent) {
        stdx::this_thread::yield();
    }

    ASSERT_EQUALS(fifoSem.value(), 0u);
    ASSERT_EQUALS(finished.load(), 0u);

    for (size_t i = 0; i < 5; ++i) {
        fifoSem.unlock();
    }

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQUALS(fifoSem.value(), 5u);
    ASSERT_EQUALS(finished.load(), nConcurrent);
}
}  // namespace
