/**
 *    Copyright (C) 2014 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/executor/network_interface_mock.h"
#include "mongo/executor/network_connection_hook.h"

#include <algorithm>
#include <iterator>

#include "mongo/stdx/functional.h"
#include "mongo/util/log.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace executor {

NetworkInterfaceMock::NetworkInterfaceMock()
    : _inNetwork(false),
      _now(fassertStatusOK(18653, dateFromISOString("2014-08-01T00:00:00Z"))),
      _hasStarted(false),
      _inShutdown(false),
      _executorNextWakeupDate(Date_t::max()) {}

NetworkInterfaceMock::~NetworkInterfaceMock() {
    invariant(!_hasStarted || _inShutdown);
    invariant(_scheduled.empty());
    invariant(_blackHoled.empty());
}

std::string NetworkInterfaceMock::getDiagnosticString() {
    // TODO something better.
    return "NetworkInterfaceMock diagnostics here";
}

void NetworkInterfaceMock::appendConnectionStats(BSONObjBuilder* b) {}

Date_t NetworkInterfaceMock::now() {
    return _now_inlock();
}

std::string NetworkInterfaceMock::getHostName() {
    return "thisisourhostname";
}

void NetworkInterfaceMock::startCommand(const TaskExecutor::CallbackHandle& cbHandle,
                                        const RemoteCommandRequest& request,
                                        const RemoteCommandCompletionFn& onFinish) {
    invariant(!_inShutdown);
    const Date_t now = _now_inlock();
    auto op = NetworkOperation(cbHandle, request, now, onFinish);

    // If we don't have a hook, or we have already 'connected' to this host, enqueue the op.
    if (!_hook || _connections.count(request.target)) {
        _enqueueOperation_inlock(std::move(op));
    } else {
        _connectThenEnqueueOperation_inlock(request.target, std::move(op));
    }
}

void NetworkInterfaceMock::setHandshakeReplyForHost(
    const mongo::HostAndPort& host, mongo::executor::RemoteCommandResponse&& reply) {
    auto it = _handshakeReplies.find(host);
    if (it == std::end(_handshakeReplies)) {
        auto res = _handshakeReplies.emplace(host, std::move(reply));
        invariant(res.second);
    } else {
        it->second = std::move(reply);
    }
}

static bool findAndCancelIf(
    const stdx::function<bool(const NetworkInterfaceMock::NetworkOperation&)>& matchFn,
    NetworkInterfaceMock::NetworkOperationList* other,
    NetworkInterfaceMock::NetworkOperationList* scheduled,
    const Date_t now) {
    const NetworkInterfaceMock::NetworkOperationIterator noi =
        std::find_if(other->begin(), other->end(), matchFn);
    if (noi == other->end()) {
        return false;
    }
    scheduled->splice(scheduled->begin(), *other, noi);
    noi->setResponse(
        now,
        TaskExecutor::ResponseStatus(ErrorCodes::CallbackCanceled, "Network operation canceled"));
    return true;
}

void NetworkInterfaceMock::cancelCommand(const TaskExecutor::CallbackHandle& cbHandle) {
    invariant(!_inShutdown);
    stdx::function<bool(const NetworkOperation&)> matchesHandle =
        stdx::bind(&NetworkOperation::isForCallback, stdx::placeholders::_1, cbHandle);
    const Date_t now = _now_inlock();
    if (findAndCancelIf(matchesHandle, &_unscheduled, &_scheduled, now)) {
        return;
    }
    if (findAndCancelIf(matchesHandle, &_blackHoled, &_scheduled, now)) {
        return;
    }
    if (findAndCancelIf(matchesHandle, &_scheduled, &_scheduled, now)) {
        return;
    }
    // No not-in-progress network command matched cbHandle.  Oh, well.
}

void NetworkInterfaceMock::setAlarm(const Date_t when, const stdx::function<void()>& action) {
    if (_hasStarted && when <= _now_inlock()) {
        bool wasInNetwork = _inNetwork;
        if (!wasInNetwork)
            enterNetwork();

        action();

        if (!wasInNetwork)
            exitNetwork();
        return;
    }

    _alarms.emplace(when, action);
}

bool NetworkInterfaceMock::onNetworkThread() {
    return _inNetwork == true;
}

void NetworkInterfaceMock::startup() {
    invariant(!_hasStarted);
    _hasStarted = true;
    _inShutdown = false;
}

void NetworkInterfaceMock::shutdown() {
    invariant(_hasStarted);
    invariant(!_inShutdown);
    _inShutdown = true;

    while (!_alarms.empty() && _now_inlock() >= _alarms.top().when) {
        auto fn = _alarms.top().action;
        _alarms.pop();

        bool wasInNetwork = _inNetwork;
        if (!wasInNetwork)
            enterNetwork();

        fn();

        if (!wasInNetwork)
            exitNetwork();
    }

    NetworkOperationList todo;
    todo.splice(todo.end(), _scheduled);
    todo.splice(todo.end(), _unscheduled);
    todo.splice(todo.end(), _processing);
    todo.splice(todo.end(), _blackHoled);

    const Date_t now = _now_inlock();
    for (NetworkOperationIterator iter = todo.begin(); iter != todo.end(); ++iter) {
        iter->setResponse(now,
                          TaskExecutor::ResponseStatus(ErrorCodes::ShutdownInProgress,
                                                       "Shutting down mock network"));
        iter->finishResponse();
    }
}

void NetworkInterfaceMock::enterNetwork() {
    _inNetwork = true;
    runReadyNetworkOperations();
}

void NetworkInterfaceMock::exitNetwork() {
    _inNetwork = false;
}

bool NetworkInterfaceMock::hasReadyRequests() {
    return _hasReadyRequests_inlock();
}

bool NetworkInterfaceMock::_hasReadyRequests_inlock() {
    if (_unscheduled.empty())
        return false;
    if (_unscheduled.front().getNextConsiderationDate() > _now_inlock()) {
        return false;
    }
    return true;
}

NetworkInterfaceMock::NetworkOperationIterator NetworkInterfaceMock::getNextReadyRequest() {
    while (!_hasReadyRequests_inlock()) {
        _runReadyNetworkOperations_inlock();
    }
    invariant(_hasReadyRequests_inlock());
    _processing.splice(_processing.begin(), _unscheduled, _unscheduled.begin());
    return _processing.begin();
}

NetworkInterfaceMock::NetworkOperationIterator NetworkInterfaceMock::getFrontOfUnscheduledQueue() {
    return _unscheduled.begin();
}

void NetworkInterfaceMock::scheduleResponse(NetworkOperationIterator noi,
                                            Date_t when,
                                            const TaskExecutor::ResponseStatus& response) {
    NetworkOperationIterator insertBefore = _scheduled.begin();
    while ((insertBefore != _scheduled.end()) && (insertBefore->getResponseDate() <= when)) {
        ++insertBefore;
    }
    noi->setResponse(when, response);
    _scheduled.splice(insertBefore, _processing, noi);
}

void NetworkInterfaceMock::blackHole(NetworkOperationIterator noi) {
    _blackHoled.splice(_blackHoled.end(), _processing, noi);
}

void NetworkInterfaceMock::requeueAt(NetworkOperationIterator noi, Date_t dontAskUntil) {
    invariant(noi->getNextConsiderationDate() < dontAskUntil);
    invariant(_now_inlock() < dontAskUntil);
    NetworkOperationIterator insertBefore = _unscheduled.begin();
    for (; insertBefore != _unscheduled.end(); ++insertBefore) {
        if (insertBefore->getNextConsiderationDate() >= dontAskUntil) {
            break;
        }
    }
    noi->setNextConsiderationDate(dontAskUntil);
    _unscheduled.splice(insertBefore, _processing, noi);
}

Date_t NetworkInterfaceMock::runUntil(Date_t until) {
    invariant(until > _now_inlock());
    while (until > _now_inlock()) {
        _runReadyNetworkOperations_inlock();
        if (_hasReadyRequests_inlock()) {
            break;
        }
        Date_t newNow = _executorNextWakeupDate;
        if (!_alarms.empty() && _alarms.top().when < newNow) {
            newNow = _alarms.top().when;
        }
        if (!_scheduled.empty() && _scheduled.front().getResponseDate() < newNow) {
            newNow = _scheduled.front().getResponseDate();
        }
        if (until < newNow) {
            newNow = until;
        }
        invariant(_now_inlock() <= newNow);
        _now = newNow;
    }
    _runReadyNetworkOperations_inlock();
    return _now_inlock();
}

void NetworkInterfaceMock::runReadyNetworkOperations() {
    _runReadyNetworkOperations_inlock();
}

void NetworkInterfaceMock::waitForWork() {
    _waitForWork_inlock();
}

void NetworkInterfaceMock::waitForWorkUntil(Date_t when) {
    _executorNextWakeupDate = when;
    if (_executorNextWakeupDate <= _now_inlock()) {
        return;
    }
    _waitForWork_inlock();
}

void NetworkInterfaceMock::_enqueueOperation_inlock(
    mongo::executor::NetworkInterfaceMock::NetworkOperation&& op) {
    auto insertBefore =
        std::upper_bound(std::begin(_unscheduled),
                         std::end(_unscheduled),
                         op,
                         [](const NetworkOperation& a, const NetworkOperation& b) {
                             return a.getNextConsiderationDate() < b.getNextConsiderationDate();
                         });

    _unscheduled.emplace(insertBefore, std::move(op));
}

void NetworkInterfaceMock::_connectThenEnqueueOperation_inlock(const HostAndPort& target,
                                                               NetworkOperation&& op) {
    invariant(_hook);  // if there is no hook, we shouldn't even hit this codepath
    invariant(!_connections.count(target));

    auto handshakeReplyIter = _handshakeReplies.find(target);

    auto handshakeReply = (handshakeReplyIter != std::end(_handshakeReplies))
        ? handshakeReplyIter->second
        : RemoteCommandResponse(BSONObj(), BSONObj(), Milliseconds(0));

    auto valid = _hook->validateHost(target, handshakeReply);
    if (!valid.isOK()) {
        op.setResponse(_now_inlock(), valid);
        op.finishResponse();
        return;
    }

    auto swHookPostconnectCommand = _hook->makeRequest(target);

    if (!swHookPostconnectCommand.isOK()) {
        op.setResponse(_now_inlock(), swHookPostconnectCommand.getStatus());
        op.finishResponse();
        return;
    }

    boost::optional<RemoteCommandRequest> hookPostconnectCommand =
        std::move(swHookPostconnectCommand.getValue());

    if (!hookPostconnectCommand) {
        // If we don't have a post connect command, enqueue the actual command.
        _enqueueOperation_inlock(std::move(op));
        _connections.emplace(op.getRequest().target);
        return;
    }

    // The completion handler for the postconnect command schedules the original command.
    auto postconnectCompletionHandler =
        [this, op](StatusWith<RemoteCommandResponse> response) mutable {
            if (!response.isOK()) {
                op.setResponse(_now_inlock(), response.getStatus());
                op.finishResponse();
                return;
            }

            auto handleStatus =
                _hook->handleReply(op.getRequest().target, std::move(response.getValue()));

            if (!handleStatus.isOK()) {
                op.setResponse(_now_inlock(), handleStatus);
                op.finishResponse();
                return;
            }

            _enqueueOperation_inlock(std::move(op));
            _connections.emplace(op.getRequest().target);
        };

    auto postconnectOp = NetworkOperation(op.getCallbackHandle(),
                                          std::move(*hookPostconnectCommand),
                                          _now_inlock(),
                                          std::move(postconnectCompletionHandler));

    _enqueueOperation_inlock(std::move(postconnectOp));
}

void NetworkInterfaceMock::setConnectionHook(std::unique_ptr<NetworkConnectionHook> hook) {
    invariant(!_hasStarted);
    invariant(!_hook);
    _hook = std::move(hook);
}

void NetworkInterfaceMock::signalWorkAvailable() {
}

void NetworkInterfaceMock::_runReadyNetworkOperations_inlock() {
    while (!_alarms.empty() && _now_inlock() >= _alarms.top().when) {
        auto fn = _alarms.top().action;
        _alarms.pop();
        fn();
    }
    while (!_scheduled.empty() && _scheduled.front().getResponseDate() <= _now_inlock()) {
        NetworkOperation op = _scheduled.front();
        _scheduled.pop_front();
        op.finishResponse();
    }
}

void NetworkInterfaceMock::_waitForWork_inlock() {
}

bool NetworkInterfaceMock::_isNetworkThreadRunnable_inlock() {
    return true;
}

bool NetworkInterfaceMock::_isExecutorThreadRunnable_inlock() {
    return true;
}

static const StatusWith<RemoteCommandResponse> kUnsetResponse(
    ErrorCodes::InternalError, "NetworkOperation::_response never set");

NetworkInterfaceMock::NetworkOperation::NetworkOperation()
    : _requestDate(),
      _nextConsiderationDate(),
      _responseDate(),
      _request(),
      _response(kUnsetResponse),
      _onFinish() {}

NetworkInterfaceMock::NetworkOperation::NetworkOperation(
    const TaskExecutor::CallbackHandle& cbHandle,
    const RemoteCommandRequest& theRequest,
    Date_t theRequestDate,
    const RemoteCommandCompletionFn& onFinish)
    : _requestDate(theRequestDate),
      _nextConsiderationDate(theRequestDate),
      _responseDate(),
      _cbHandle(cbHandle),
      _request(theRequest),
      _response(kUnsetResponse),
      _onFinish(onFinish) {}

NetworkInterfaceMock::NetworkOperation::~NetworkOperation() {}

void NetworkInterfaceMock::NetworkOperation::setNextConsiderationDate(
    Date_t nextConsiderationDate) {
    invariant(nextConsiderationDate > _nextConsiderationDate);
    _nextConsiderationDate = nextConsiderationDate;
}

void NetworkInterfaceMock::NetworkOperation::setResponse(
    Date_t responseDate, const TaskExecutor::ResponseStatus& response) {
    invariant(responseDate >= _requestDate);
    _responseDate = responseDate;
    _response = response;
}

void NetworkInterfaceMock::NetworkOperation::finishResponse() {
    invariant(_onFinish);
    _onFinish(_response);
    _onFinish = RemoteCommandCompletionFn();
}

}  // namespace executor
}  // namespace mongo
