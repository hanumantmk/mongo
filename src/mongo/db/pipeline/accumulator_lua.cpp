/**
 * Copyright (c) 2011 10gen Inc.
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
 * must comply with the GNU Affero General Public License in all respects for
 * all of the code used other than as permitted herein. If you modify file(s)
 * with this exception, you may extend this exception to your version of the
 * file(s), but you are not obligated to do so. If you do not wish to do so,
 * delete this exception statement from your version. If you delete this
 * exception statement from all source files in the program, then also delete
 * it in the license file.
 */

#include "mongo/pch.h"

#include "mongo/db/pipeline/accumulator.h"
#include "mongo/db/pipeline/value.h"

namespace mongo {

    void AccumulatorLua::processInternal(const Value& arg, bool merging) {
        if (merging) {
            lua_getglobal(L, "mprocess");
        } else {
            lua_getglobal(L, "process");
        }

        if (arg.getType() == NumberDouble) {
            lua_pushnumber(L, arg.coerceToDouble());
        } else if (arg.getType() == NumberLong) {
            lua_pushinteger(L, arg.coerceToLong());
        } else if (arg.getType() == NumberInt) {
            lua_pushinteger(L, arg.coerceToInt());
        } else if (arg.getType() == String) {
            lua_pushstring(L, arg.coerceToString().c_str());
        } else {
            uassert(-1, "shouldn't be here", 0);
        }

        if (lua_pcall(L, 1, 0, 0)) {
            std::cout << "error is: " << lua_tostring(L, -1) << "\n";
        }

    }

    intrusive_ptr<Accumulator> AccumulatorLua::create(const Value& input) {
        return new AccumulatorLua(input);
    }

    Value AccumulatorLua::getValue(bool toBeMerged) const {
        if (toBeMerged) {
            lua_getglobal(L, "mget");
        } else {
            lua_getglobal(L, "get");
        }

        if (lua_pcall(L, 0, 1, 0)) {
            std::cout << "error is: " << lua_tostring(L, -1) << "\n";
        }

        Value out;

        switch(lua_type(L, -1)) {
            case LUA_TNUMBER:
                out = Value(lua_tonumber(L, -1));
                break;
            case LUA_TBOOLEAN:
                out = Value(lua_toboolean(L, -1));
                break;
            case LUA_TSTRING:
                out = Value(lua_tostring(L, -1));
                break;
            default:
                break;
        }

        lua_pop(L, 1);

        return out;
    }

    AccumulatorLua::~AccumulatorLua() {
        lua_close(L);
    }

    AccumulatorLua::AccumulatorLua(const Value& i) : input(i)
    {
        // This is a fixed size Accumulator so we never need to update this
        _memUsageBytes = sizeof(*this);

        L = luaL_newstate();

        luaL_openlibs(L);

        uassert(-1, "failure in compiling lua", ! luaL_dostring(L, input.coerceToString().c_str()));

    }

    void AccumulatorLua::reset() {
        lua_getglobal(L, "reset");

        if (lua_pcall(L, 0, 0, 0)) {
            std::cout << "error is: " << lua_tostring(L, -1) << "\n";
        }
    }

    const char *AccumulatorLua::getOpName() const {
        return "$lua";
    }

}
