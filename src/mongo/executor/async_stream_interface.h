/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#include <asio.hpp>
#include <memory>
#include <system_error>

#include "mongo/base/data_range.h"
#include "mongo/base/disallow_copying.h"
#include "mongo/base/status_with.h"
#include "mongo/stdx/functional.h"

namespace mongo {
namespace executor {

class PollReactor;

/**
 * A bidirectional stream supporting asynchronous reads and writes.
 */
class AsyncStreamInterface {
    MONGO_DISALLOW_COPYING(AsyncStreamInterface);

public:
    virtual ~AsyncStreamInterface() = default;

    using ConnectHandler = stdx::function<void(std::error_code)>;

    using StreamHandler = stdx::function<void(std::error_code, std::size_t)>;

    virtual void connect(asio::ip::tcp::resolver::iterator endpoints,
                         ConnectHandler&& connectHandler) = 0;

    virtual void write(asio::const_buffer buf, StreamHandler&& writeHandler) = 0;

    virtual void read(asio::mutable_buffer buf, StreamHandler&& readHandler) = 0;

    virtual void cancel() = 0;

    virtual bool isOpen() = 0;

    virtual StatusWith<size_t> syncRead(DataRange) = 0;

    virtual StatusWith<size_t> syncWrite(ConstDataRange) = 0;

    virtual int nativeHandle() = 0;

    virtual void setReactor(PollReactor*) = 0;

    virtual PollReactor* getReactor() const = 0;

protected:
    AsyncStreamInterface() = default;
};

}  // namespace executor
}  // namespace mongo
