/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "mongo/platform/basic.h"

#include <exception>
#include <string>
#include <vector>

#include "mongo/base/data_cursor.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/data_type_endian.h"
#include "mongo/base/data_view.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/rpc/message.h"
#include "mongo/rpc/op_msg.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/time_support.h"

using namespace mongo;

class Done : public std::exception {};

struct Packet {
    uint64_t id;
    StringData local;
    StringData remote;
    Date_t date;
    MsgData::ConstView message;
};

void readBytes(size_t toRead, char* buf) {
    while (toRead) {
        auto r = ::read(0, buf, toRead);

        if (r == -1) {
            if (errno == EINTR) {
                continue;
            }

            throw std::runtime_error("failed to read bytes");
        } else if (r == 0) {
            throw Done();
        }

        buf += r;
        toRead -= r;
    }
}

Packet readPacket(char* buf) {
    readBytes(4, buf);
    auto len = ConstDataView(buf).read<LittleEndian<uint32_t>>();

    std::cout << "len: " << len << std::endl;

    if (len > (1 << 26)) {
        throw std::runtime_error("packet too large");
    }

    readBytes(len - 4, buf + 4);

    ConstDataRangeCursor cdr(buf, buf + len);

    uassertStatusOK(cdr.skip<LittleEndian<uint32_t>>());
    uint64_t id = uassertStatusOK(cdr.readAndAdvance<LittleEndian<uint64_t>>());
    StringData local = uassertStatusOK(cdr.readAndAdvance<Terminated<'\0', StringData>>());
    StringData remote = uassertStatusOK(cdr.readAndAdvance<Terminated<'\0', StringData>>());
    uint64_t date = uassertStatusOK(cdr.readAndAdvance<LittleEndian<uint64_t>>());
    MsgData::ConstView message(cdr.data());

    return {id, local, remote, Date_t::fromMillisSinceEpoch(date), message};
}

template <NetworkOp>
void handle(MsgData::ConstView message) {}

template <>
void handle<opReply>(MsgData::ConstView message) {
    ConstDataRangeCursor cdrc(message.data(), message.data() + message.dataLen());
    auto responseFlags = uassertStatusOK(cdrc.readAndAdvance<LittleEndian<int32_t>>());
    auto cursorID = uassertStatusOK(cdrc.readAndAdvance<LittleEndian<int64_t>>());
    auto startingFrom = uassertStatusOK(cdrc.readAndAdvance<LittleEndian<int32_t>>());
    auto numberReturned = uassertStatusOK(cdrc.readAndAdvance<LittleEndian<int32_t>>());

    std::vector<BSONObj> docs;
    for (int i = 0; i < numberReturned; ++i) {
        docs.push_back(uassertStatusOK(cdrc.readAndAdvance<BSONObj>()));
    }

    std::cout << "    \"responseFlags\": " << responseFlags << ",\n";
    std::cout << "    \"cursorID\": " << cursorID << ",\n";
    std::cout << "    \"startingFrom\": " << startingFrom << ",\n";
    std::cout << "    \"numberReturned\": " << numberReturned << ",\n";
    std::cout << "    \"documents\": [\n";
    for (const auto& doc : docs) {
        std::cout << "      " << doc.toString() << ",\n";
    }
    std::cout << "    ],\n";
}

template <>
void handle<dbMsg>(MsgData::ConstView view) {
    auto buf = SharedBuffer::allocate(view.getLen());
    std::memcpy(buf.get(), view.view2ptr(), view.getLen());
    Message message(buf);
    auto opMsg = OpMsg::parseOwned(message);

    std::cout << "    \"flagBits\": " << OpMsg::flags(message) << ",\n";
    std::cout << "    \"body\": " << opMsg.body << ",\n";
    std::cout << "    \"documentSequences\": [\n";
    for (const auto& seq : opMsg.sequences) {
        std::cout << "      \"" << seq.name << "\": [\n";
        for (const auto& doc : seq.objs) {
            std::cout << "        " << doc << ",\n";
        }
        std::cout << "      ],\n";
    }
    std::cout << "    ],\n";
}

template <>
void handle<dbCompressed>(MsgData::ConstView message) {
    ConstDataRangeCursor cdrc(message.data(), message.data() + message.dataLen());
    auto originalCode = uassertStatusOK(cdrc.readAndAdvance<LittleEndian<int32_t>>());
    auto uncompressedSize = uassertStatusOK(cdrc.readAndAdvance<LittleEndian<int32_t>>());
    auto compressorId = uassertStatusOK(cdrc.readAndAdvance<LittleEndian<uint8_t>>());

    std::cout << "    \"originalCode\": " << originalCode << ",\n";
    std::cout << "    \"uncompressedSize\": " << uncompressedSize << ",\n";
    std::cout << "    \"compressorId\": " << int(compressorId) << ",\n";
}

void handleMessage(MsgData::ConstView message) {
    switch (message.getNetworkOp()) {
        case opReply:
            return handle<opReply>(message);
        case dbUpdate:
            return handle<dbUpdate>(message);
        case dbInsert:
            return handle<dbInsert>(message);
        case dbQuery:
            return handle<dbQuery>(message);
        case dbGetMore:
            return handle<dbGetMore>(message);
        case dbDelete:
            return handle<dbDelete>(message);
        case dbKillCursors:
            return handle<dbKillCursors>(message);
        case dbCompressed:
            return handle<dbCompressed>(message);
        case dbMsg:
            return handle<dbMsg>(message);
        default:
            break;
    }
}

int main(int argc, char** argv) {
    auto buf = SharedBuffer::allocate(1 << 26);

    try {
        while (true) {
            auto packet = readPacket(buf.get());

            std::cout << "{\n";
            std::cout << "  \"id\": " << packet.id << ",\n";
            std::cout << "  \"local\": " << packet.local.toString() << ",\n";
            std::cout << "  \"remote\": " << packet.remote.toString() << ",\n";
            std::cout << "  \"date\": " << packet.date.toString() << ",\n";
            std::cout << "  \"message\": {\n";
            std::cout << "    \"messageLength\": " << packet.message.getLen() << ",\n";
            std::cout << "    \"requestID\": " << packet.message.getId() << ",\n";
            std::cout << "    \"responseTo\": " << packet.message.getResponseToMsgId() << ",\n";
            std::cout << "    \"opCode\": " << packet.message.getNetworkOp() << ",\n";
            handleMessage(packet.message);
            std::cout << "  },\n";
            std::cout << "}\n";
        }
    } catch (const Done&) {
    }

    return 0;
}
