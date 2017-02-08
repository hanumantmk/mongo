/**
 *    Copyright (C) 2017 MongoDB Inc.
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

#include "mongo/platform/basic.h"

#include <iostream>

#include "mongo/util/inproc_pipe.h"

namespace mongo {

InprocPipe::InprocPipe(size_t capacity) : _buffer(capacity) {}

void InprocPipe::close() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);

    _closed = true;

    _condvar.notify_all();
}

InprocPipe::Result InprocPipe::send(const uint8_t* buffer, size_t bufferLen, stdx::chrono::milliseconds timeout) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto deadline = stdx::chrono::system_clock::now() + timeout;

    while (bufferLen) {
        if (! _condvar.wait_until(lk, deadline, [&]{
            return !_buffer.full() || _closed;
        })) {
            return _closed ? Result::Closed : Result::Timeout;
        }

        if (_closed) {
            return Result::Closed;
        }

        auto toWrite = std::min(bufferLen, _buffer.reserve());

        std::copy(buffer, buffer + toWrite, std::back_inserter(_buffer));

        buffer += toWrite;
        bufferLen -= toWrite;
    }

    _condvar.notify_all();

    return Result::Success;
}

InprocPipe::Result InprocPipe::recv(uint8_t* buffer, size_t bufferLen, stdx::chrono::milliseconds timeout) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto deadline = stdx::chrono::system_clock::now() + timeout;

    while (bufferLen) {
        if (! _condvar.wait_until(lk, deadline, [&]{
            return !_buffer.empty() || _closed;
        })) {
            return _closed ? Result::Closed : Result::Timeout;
        }

        if (_closed) {
            return Result::Closed;
        }

        auto toRead = std::min(bufferLen, _buffer.size());

        std::copy(_buffer.begin(), _buffer.begin() + toRead, buffer);
        _buffer.erase_begin(toRead);

        buffer += toRead;
        bufferLen -= toRead;
    }

    _condvar.notify_all();

    return Result::Success;
}

}  // namespace mongo
