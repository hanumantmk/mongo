/*    Copyright 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
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

#include "mongo/platform/basic.h"

#include "mongo/db/client.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/proxyscope.h"

namespace mongo {
namespace mozjs {

void ProxyScope::wrapCall(std::function<void()> f) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _function = std::move(f);
    _implDone = false;
    _proxyDone = true;

    _condvar.notify_one();

    _condvar.wait(lk, [this] { return _implDone; });

    // It seems like C++ gets unhappy if you uassert the Status directly on
    // the object.  Otherwise you get multiple exception in flight
    // terminates...
    Status s = std::move(_status);
    uassertStatusOK(s);
}

std::string ProxyScope::getError() {
    std::string out;
    wrapCall([&] { out = _implScope->getError(); });
    return out;
}

void ProxyScope::reset() {
    wrapCall([&] { _implScope->reset(); });
}

void ProxyScope::registerOperation(OperationContext* txn) {
    wrapCall([&] { _implScope->registerOperation(txn); });
}

void ProxyScope::unregisterOperation() {
    wrapCall([&] { _implScope->unregisterOperation(); });
}

void ProxyScope::kill() {
    _implScope->kill();
}

/** check if there is a pending killOp request */
bool ProxyScope::isKillPending() const {
    return _implScope->isKillPending();
}

OperationContext* ProxyScope::getOpContext() const {
    return _implScope->getOpContext();
}

void ProxyScope::implThread() {
    if (hasGlobalServiceContext())
        Client::initThread("js");

    std::unique_ptr<ImplScope> scope;

    try {
        scope.reset(new ImplScope(_engine));
        _implScope = scope.get();
    } catch (...) {
        _status = exceptionToStatus();
    }

    for (;;) {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        _condvar.wait(lk, [this] { return _proxyDone; });

        if (_threadDone)
            break;

        try {
            _function();
        } catch (...) {
            _status = exceptionToStatus();
        }

        _implDone = true;
        _proxyDone = false;

        _condvar.notify_one();
    }
}

ProxyScope::ProxyScope(ScriptEngine* engine)
    : _engine(engine),
      _implScope(nullptr),
      _proxyDone(false),
      _implDone(false),
      _threadDone(false),
      _mutex(),
      _condvar(),
      _status(Status::OK()),
      _thread(&ProxyScope::implThread, this) {
    // make sure we our child thread is actually up in the constructor

    try {
        wrapCall([] {});
    } catch (...) {
        shutdownThread();
        throw;
    }
}

ProxyScope::~ProxyScope() {
    kill();

    shutdownThread();
}

void ProxyScope::shutdownThread() {
    {
        stdx::unique_lock<stdx::mutex> lk(_mutex);

        _threadDone = true;
        _proxyDone = true;
    }

    _condvar.notify_one();

    _thread.join();
}

bool ProxyScope::hasOutOfMemoryException() {
    return false;
}

void ProxyScope::init(const BSONObj* data) {
    wrapCall([&] { _implScope->init(data); });
}

void ProxyScope::setNumber(const char* field, double val) {
    wrapCall([&] { _implScope->setNumber(field, val); });
}

void ProxyScope::setString(const char* field, StringData val) {
    wrapCall([&] { _implScope->setString(field, val); });
}

void ProxyScope::setBoolean(const char* field, bool val) {
    wrapCall([&] { _implScope->setBoolean(field, val); });
}

void ProxyScope::setElement(const char* field, const BSONElement& e) {
    wrapCall([&] { _implScope->setElement(field, e); });
}

void ProxyScope::setObject(const char* field, const BSONObj& obj, bool readOnly) {
    wrapCall([&] { _implScope->setObject(field, obj, readOnly); });
}

int ProxyScope::type(const char* field) {
    int out;
    wrapCall([&] { out = _implScope->type(field); });
    return out;
}

double ProxyScope::getNumber(const char* field) {
    double out;
    wrapCall([&] { out = _implScope->getNumber(field); });
    return out;
}

int ProxyScope::getNumberInt(const char* field) {
    int out;
    wrapCall([&] { out = _implScope->getNumberInt(field); });
    return out;
}

long long ProxyScope::getNumberLongLong(const char* field) {
    long long out;
    wrapCall([&] { out = _implScope->getNumberLongLong(field); });
    return out;
}

std::string ProxyScope::getString(const char* field) {
    std::string out;
    wrapCall([&] { out = _implScope->getString(field); });
    return out;
}

bool ProxyScope::getBoolean(const char* field) {
    bool out;
    wrapCall([&] { out = _implScope->getBoolean(field); });
    return out;
}

BSONObj ProxyScope::getObject(const char* field) {
    BSONObj out;
    wrapCall([&] { out = _implScope->getObject(field); });
    return out;
}

ScriptingFunction ProxyScope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
    ScriptingFunction out;
    wrapCall([&] { out = _implScope->_createFunction(raw, functionNumber); });
    return out;
}

void ProxyScope::setFunction(const char* field, const char* code) {
    wrapCall([&] { _implScope->setFunction(field, code); });
}

void ProxyScope::rename(const char* from, const char* to) {
    wrapCall([&] { _implScope->rename(from, to); });
}

int ProxyScope::invoke(ScriptingFunction func,
                       const BSONObj* argsObject,
                       const BSONObj* recv,
                       int timeoutMs,
                       bool ignoreReturn,
                       bool readOnlyArgs,
                       bool readOnlyRecv) {
    int out;
    wrapCall([&] {
        out = _implScope->invoke(
            func, argsObject, recv, timeoutMs, ignoreReturn, readOnlyArgs, readOnlyRecv);
    });

    return out;
}

bool ProxyScope::exec(StringData code,
                      const std::string& name,
                      bool printResult,
                      bool reportError,
                      bool assertOnError,
                      int timeoutMs) {
    bool out;
    wrapCall([&] {
        out = _implScope->exec(code, name, printResult, reportError, assertOnError, timeoutMs);
    });
    return out;
}

void ProxyScope::injectNative(const char* field, NativeFunction func, void* data) {
    wrapCall([&] { _implScope->injectNative(field, func, data); });
}

void ProxyScope::gc() {
    _implScope->gc();
}

void ProxyScope::localConnectForDbEval(OperationContext* txn, const char* dbName) {
    wrapCall([&] { _implScope->localConnectForDbEval(txn, dbName); });
}

void ProxyScope::externalSetup() {
    wrapCall([&] { _implScope->externalSetup(); });
}

}  // namespace mozjs
}  // namespace mongo
