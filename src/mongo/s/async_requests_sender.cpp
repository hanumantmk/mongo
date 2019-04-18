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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include "mongo/s/async_requests_sender.h"

#include "mongo/client/remote_command_targeter.h"
#include "mongo/executor/remote_command_request.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/grid.h"
#include "mongo/stdx/memory.h"
#include "mongo/transport/baton.h"
#include "mongo/transport/transport_layer.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {

namespace {

// Maximum number of retries for network and replication notMaster errors (per host).
const int kMaxNumFailedHostRetryAttempts = 3;

}  // namespace

AsyncRequestsSender::AsyncRequestsSender(OperationContext* opCtx,
                                         executor::TaskExecutor* executor,
                                         StringData dbName,
                                         const std::vector<AsyncRequestsSender::Request>& requests,
                                         const ReadPreferenceSetting& readPreference,
                                         Shard::RetryPolicy retryPolicy)
    : _opCtx(opCtx),
      _db(dbName.toString()),
      _readPreference(readPreference),
      _retryPolicy(retryPolicy),
      _executor(executor),
      _baton(opCtx->getBaton()->makeSubBaton()) {

    _remotes.reserve(requests.size());
    for (const auto& request : requests) {
        auto cmdObj = request.cmdObj;
        _remotes.emplace_back(this, request.shardId, cmdObj);
    }
    _remotesLeft = _remotes.size();

    // Initialize command metadata to handle the read preference.
    _metadataObj = readPreference.toContainingBSON();

    // Kick off requests immediately.
    for (auto& remote : _remotes) {
        remote.executeRequest();
    }
}

AsyncRequestsSender::Response AsyncRequestsSender::next() noexcept {
    invariant(!done());

    _remotesLeft--;

    // If we've been interrupted, the ready queue should be filled with interrupted answers, go
    // ahead and return one of those
    if (!_interruptStatus.isOK()) {
        return std::move(*(_readyQueue.pop(_opCtx)->response));
    }

    // Try to pop a value from the queue
    try {
        return std::move(*(_readyQueue.pop(_opCtx)->response));
    } catch (const DBException& ex) {
        // If we're interrupted, save that value and overwrite all outstanding requests (that we're
        // not going to wait to collect)
        _interruptStatus = ex.toStatus();
    }

    // Overwrite any outstanding remotes with the interruption status and push them onto the ready
    // queue
    for (auto& remote : _remotes) {
        if (!remote.response) {
            remote.response = Response(remote.shardId, _interruptStatus, remote.shardHostAndPort);
            _readyQueue.push(&remote);
        }
    }

    // Stop servicing callbacks
    _baton.shutdown();

    // shutdown the scoped task executor
    _executor.shutdown();

    return *(_readyQueue.pop(_opCtx)->response);
}

void AsyncRequestsSender::stopRetrying() noexcept {
    _stopRetrying = true;
}

bool AsyncRequestsSender::done() noexcept {
    return !_remotesLeft;
}

AsyncRequestsSender::Request::Request(ShardId shardId, BSONObj cmdObj)
    : shardId(shardId), cmdObj(cmdObj) {}

AsyncRequestsSender::Response::Response(ShardId shardId,
                                        executor::RemoteCommandResponse response,
                                        HostAndPort hp)
    : shardId(std::move(shardId)),
      swResponse(std::move(response)),
      shardHostAndPort(std::move(hp)) {}

AsyncRequestsSender::Response::Response(ShardId shardId,
                                        Status status,
                                        boost::optional<HostAndPort> hp)
    : shardId(std::move(shardId)), swResponse(std::move(status)), shardHostAndPort(std::move(hp)) {}

AsyncRequestsSender::RemoteData::RemoteData(AsyncRequestsSender* ars,
                                            ShardId shardId,
                                            BSONObj cmdObj)
    : ars(ars), shardId(std::move(shardId)), cmdObj(std::move(cmdObj)) {}

std::shared_ptr<Shard> AsyncRequestsSender::RemoteData::getShard() {
    // TODO: Pass down an OperationContext* to use here.
    return Grid::get(getGlobalServiceContext())->shardRegistry()->getShardNoReload(shardId);
}

void AsyncRequestsSender::RemoteData::executeRequest() {
    scheduleRequest().getAsync([this](StatusWith<executor::RemoteCommandResponse> rcr) {
        response = [&] {
            if (rcr.isOK()) {
                return Response(shardId, std::move(rcr.getValue()), *shardHostAndPort);
            } else {
                return Response(shardId, std::move(rcr.getStatus()), shardHostAndPort);
            }
        }();

        ars->_readyQueue.push(this);
    });
}

ExecutorFuture<executor::RemoteCommandResponse> AsyncRequestsSender::RemoteData::scheduleRequest() {
    return resolveShardIdToHostAndPort(ars->_readPreference)
        .then([this](const auto& hostAndPort) { return scheduleRemoteCommand(hostAndPort); })
        .then([this](auto&& rcr) { return handleResponse(std::move(rcr)); });
}

ExecutorFuture<HostAndPort> AsyncRequestsSender::RemoteData::resolveShardIdToHostAndPort(
    const ReadPreferenceSetting& readPref) {
    const auto shard = getShard();
    if (!shard) {
        return {
            *ars->_baton,
            Status(ErrorCodes::ShardNotFound, str::stream() << "Could not find shard " << shardId)};
    }

    return shard->getTargeter()->findHostWithMaxWait(readPref, Seconds(20)).thenRunOn(*ars->_baton);
}

ExecutorFuture<executor::RemoteCommandResponse>
AsyncRequestsSender::RemoteData::scheduleRemoteCommand(const HostAndPort& hostAndPort) {
    shardHostAndPort = hostAndPort;

    executor::RemoteCommandRequest request(
        *shardHostAndPort, ars->_db, cmdObj, ars->_metadataObj, ars->_opCtx);

    // We have to make a promise future pair because the TaskExecutor doesn't currently support a
    // future returning variant of scheduleRemoteCommand
    auto[p, f] = makePromiseFuture<executor::RemoteCommandResponse>();

    // Failures to schedule skip the retry loop
    uassertStatusOK(ars->_executor->scheduleRemoteCommand(
        request,
        // We have to make a shared promise here because scheduleRemoteCommand requires copyable
        // callbacks
        [p = std::make_shared<Promise<executor::RemoteCommandResponse>>(std::move(p))](
            const executor::TaskExecutor::RemoteCommandCallbackArgs& cbData) {
            p->emplaceValue(cbData.response);
        },
        *ars->_baton));

    return std::move(f).thenRunOn(*ars->_baton);
}

ExecutorFuture<executor::RemoteCommandResponse> AsyncRequestsSender::RemoteData::handleResponse(
    executor::RemoteCommandResponse&& rcr) {

    auto status = rcr.status;

    if (status.isOK()) {
        status = getStatusFromCommandResult(rcr.data);
    }

    if (status.isOK()) {
        status = getWriteConcernStatusFromCommandResult(rcr.data);
    }

    // If we're okay (RemoteCommandResponse, command result and write concern)-wise we're done.
    // Otherwise check for retryability
    if (status.isOK()) {
        return {*ars->_baton, std::move(rcr)};
    }

    // There was an error with either the response or the command.
    auto shard = getShard();
    if (!shard) {
        uasserted(ErrorCodes::ShardNotFound, str::stream() << "Could not find shard " << shardId);
    } else {
        if (shardHostAndPort) {
            shard->updateReplSetMonitor(*shardHostAndPort, status);
        }
        if (!ars->_stopRetrying && shard->isRetriableError(status.code(), ars->_retryPolicy) &&
            retryCount < kMaxNumFailedHostRetryAttempts) {
            LOG(1) << "Command to remote " << shardId << " at host " << *shardHostAndPort
                   << " failed with retriable error and will be retried "
                   << causedBy(redact(status));
            ++retryCount;
            shardHostAndPort.reset();
            // retry through recursion
            return scheduleRequest();
        }
    }

    // Status' in the response.status field that aren't retried get converted to top level errors
    uassertStatusOK(rcr.status);

    // We're not okay (on the remote), but still not going to retry
    return {*ars->_baton, std::move(rcr)};
};

}  // namespace mongo
