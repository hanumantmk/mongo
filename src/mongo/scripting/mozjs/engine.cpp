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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include "mongo/scripting/mozjs/engine.h"

#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/proxyscope.h"
#include "mongo/util/log.h"

namespace mongo {

void ScriptEngine::setup() {
    if (!globalScriptEngine) {
        globalScriptEngine = new mozjs::ScriptEngine();

        if (hasGlobalServiceContext()) {
            getGlobalServiceContext()->registerKillOpListener(globalScriptEngine);
        }
    }
}

std::string ScriptEngine::getInterpreterVersionString() {
    return "MozJS 38";
}

namespace mozjs {

ScriptEngine::ScriptEngine() {
    JS_Init();
}

ScriptEngine::~ScriptEngine() {
    JS_ShutDown();
}

mongo::Scope* ScriptEngine::createScope() {
    return new ProxyScope(this);
}

void ScriptEngine::interrupt(unsigned opId) {
    boost::lock_guard<boost::mutex> intLock(_globalInterruptLock);
    OpIdToScopeMap::iterator iScope = _opToScopeMap.find(opId);
    if (iScope == _opToScopeMap.end()) {
        // got interrupt request for a scope that no longer exists
        LOG(1) << "received interrupt request for unknown op: " << opId << printKnownOps_inlock()
               << std::endl;
        return;
    }
    LOG(1) << "interrupting op: " << opId << printKnownOps_inlock() << std::endl;
    iScope->second->kill();
}

std::string ScriptEngine::printKnownOps_inlock() {
    str::stream out;

    if (shouldLog(logger::LogSeverity::Debug(2))) {
        out << "  known ops: \n";

        for (OpIdToScopeMap::iterator iSc = _opToScopeMap.begin(); iSc != _opToScopeMap.end();
             ++iSc) {
            out << "  " << iSc->first << "\n";
        }
    }

    return out;
}

void ScriptEngine::interruptAll() {
    boost::lock_guard<boost::mutex> interruptLock(_globalInterruptLock);

    for (auto iScope = _opToScopeMap.begin(); iScope != _opToScopeMap.end(); ++iScope) {
        iScope->second->kill();
    }
}

}  // namespace mozjs
}  // namespace mongo
