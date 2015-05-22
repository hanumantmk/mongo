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

#pragma once

#include <map>

#include "mongo/util/concurrency/mutex.h"
#include "mongo/scripting/engine.h"
#include "mongo/scripting/deadline_monitor.h"

namespace mongo {
namespace mozjs {

class ImplScope;
class ProxyScope;

class ScriptEngine : public mongo::ScriptEngine {
    friend ImplScope;
    friend ProxyScope;

public:
    ScriptEngine();
    virtual ~ScriptEngine();

    virtual mongo::Scope* createScope();

    virtual void runTest() {}

    bool utf8Ok() const {
        return true;
    }

    virtual void interrupt(unsigned opId);

    virtual void interruptAll();

    DeadlineMonitor<ImplScope>* getDeadlineMonitor() {
        return &_deadlineMonitor;
    }

private:
    typedef std::map<unsigned, ImplScope*> OpIdToScopeMap;

    mongo::mutex _globalInterruptLock;  // protects map of all operation ids -> scope

    OpIdToScopeMap _opToScopeMap;  // map of mongo op ids to scopes (protected by
                                   // _globalInterruptLock).
    DeadlineMonitor<ImplScope> _deadlineMonitor;

    std::string printKnownOps_inlock();
};

extern ScriptEngine* globalScriptEngine;

}  // namespace mozjs
}  // namespace mongo
