// dbmessage.h

/**
*    Copyright (C) 2008 10gen Inc.
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

#include "mongo/bson/bson_validate.h"
#include "mongo/client/constants.h"
#include "mongo/db/jsobj.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/message_port.h"

namespace mongo {

    /* db response format

       Query or GetMore: // see struct QueryResult
          int resultFlags;
          int64 cursorID;
          int startingFrom;
          int nReturned;
          list of marshalled JSObjects;
    */

/* db request message format

   unsigned opid;         // arbitary; will be echoed back
   byte operation;
   int options;

   then for:

   dbInsert:
      std::string collection;
      a series of JSObjects
   dbDelete:
      std::string collection;
      int flags=0; // 1=DeleteSingle
      JSObject query;
   dbUpdate:
      std::string collection;
      int flags; // 1=upsert
      JSObject query;
      JSObject objectToUpdate;
        objectToUpdate may include { $inc: <field> } or { $set: ... }, see struct Mod.
   dbQuery:
      std::string collection;
      int nToSkip;
      int nToReturn; // how many you want back as the beginning of the cursor data (0=no limit)
                     // greater than zero is simply a hint on how many objects to send back per "cursor batch".
                     // a negative number indicates a hard limit.
      JSObject query;
      [JSObject fieldsToReturn]
   dbGetMore:
      std::string collection; // redundant, might use for security.
      int nToReturn;
      int64 cursorID;
   dbKillCursors=2007:
      int n;
      int64 cursorIDs[n];

   Note that on Update, there is only one object, which is different
   from insert where you can pass a list of objects to insert in the db.
   Note that the update field layout is very similar layout to Query.
*/

#include "mongo/db/encoded_value_query_result.h"

    /* For the database/server protocol, these objects and functions encapsulate
       the various messages transmitted over the connection.

       See http://dochub.mongodb.org/core/mongowireprotocol
    */
    class DbMessage {
    public:
        DbMessage(const Message& _m) : m(_m), reserved(0), mark(0) {
            // for received messages, Message has only one buffer
            theEnd = _m.singleData()->_data().ptr() + _m.header()->dataLen();
            char *r = _m.singleData()->_data().ptr();
            reserved = r;
            data = r + 4;
            nextjsobj = data;
        }

        /** the 32 bit field before the ns 
         * track all bit usage here as its cross op
         * 0: InsertOption_ContinueOnError
         * 1: fromWriteback
         */
        encoded_value::Reference<int>
        reservedField() { return reserved.ptr(); }

        const char * getns() const {
            return data;
        }

        const char * afterNS() const {
            return data + strlen( data ) + 1;
        }

        int getInt( int num ) const {
            encoded_value::CPointer<int> foo(afterNS());
            return foo[num];
        }

        int getQueryNToReturn() const {
            return getInt( 1 );
        }

        /**
         * get an int64 at specified offsetBytes after ns
         */
        long long getInt64( int offsetBytes ) const {
            const char * x = afterNS();
            x += offsetBytes;
            return encoded_value::CReference<long long>(x);
        }

        void resetPull() { nextjsobj = data; }
        int pullInt() const { return pullInt(); }
        encoded_value::Reference<int> pullInt() {
            if ( nextjsobj == data )
                nextjsobj += strlen(data) + 1; // skip namespace
            encoded_value::Reference<int> i(const_cast<char *>(nextjsobj));
            nextjsobj += 4;
            return i;
        }
        long long pullInt64() const {
            return pullInt64();
        }
        encoded_value::Reference<long long> pullInt64() {
            if ( nextjsobj == data )
                nextjsobj += strlen(data) + 1; // skip namespace
            encoded_value::Reference<long long> i(const_cast<char *>(nextjsobj));
            nextjsobj += 8;
            return i;
        }

        OID* getOID() const {
            return (OID *) (data + strlen(data) + 1); // skip namespace
        }

        void getQueryStuff(const char *&query, int& ntoreturn) {
            encoded_value::CPointer<int> i(data + strlen(data) + 1);
            ntoreturn = *i;
            i++;
            query = i.ptr();
        }

        /* for insert and update msgs */
        bool moreJSObjs() const {
            return nextjsobj != 0;
        }
        BSONObj nextJsObj() {
            if ( nextjsobj == data ) {
                nextjsobj += strlen(data) + 1; // skip namespace
                massert( 13066 ,  "Message contains no documents", theEnd > nextjsobj );
            }
            massert( 10304,
                     "Client Error: Remaining data too small for BSON object",
                     theEnd - nextjsobj >= 5 );

            if (serverGlobalParams.objcheck) {
                Status status = validateBSON( nextjsobj, theEnd - nextjsobj );
                massert( 10307,
                         str::stream() << "Client Error: bad object in message: " << status.reason(),
                         status.isOK() );
            }

            BSONObj js(nextjsobj);
            verify( js.objsize() >= 5 );
            verify( js.objsize() < ( theEnd - data ) );

            nextjsobj += js.objsize();
            if ( nextjsobj >= theEnd )
                nextjsobj = 0;
            return js;
        }

        const Message& msg() const { return m; }

        const char * markGet() {
            return nextjsobj;
        }

        void markSet() {
            mark = nextjsobj;
        }

        void markReset( const char * toMark = 0) {
            if( toMark == 0 ) toMark = mark;
            verify( toMark );
            nextjsobj = toMark;
        }

    private:
        const Message& m;
        encoded_value::Pointer<int> reserved;
        const char *data;
        const char *nextjsobj;
        const char *theEnd;

        const char * mark;
    };


    /* a request to run a query, received from the database */
    class QueryMessage {
    public:
        const char *ns;
        int ntoskip;
        int ntoreturn;
        int queryOptions;
        BSONObj query;
        BSONObj fields;

        /**
         * parses the message into the above fields
         * Warning: constructor mutates DbMessage.
         */
        QueryMessage(DbMessage& d) {
            ns = d.getns();
            ntoskip = d.pullInt();
            ntoreturn = d.pullInt();
            query = d.nextJsObj();
            if ( d.moreJSObjs() ) {
                fields = d.nextJsObj();
            }
            queryOptions = d.msg().header()->dataAsInt();
        }
    };

    /**
     * A response to a DbMessage.
     */
    struct DbResponse {
        Message *response;
        MSGID responseTo;
        std::string exhaustNS; /* points to ns if exhaust mode. 0=normal mode*/
        DbResponse(Message *r, MSGID rt) : response(r), responseTo(rt){ }
        DbResponse() {
            response = 0;
        }
        ~DbResponse() { delete response; }
    };

    void replyToQuery(int queryResultFlags,
                      AbstractMessagingPort* p, Message& requestMsg,
                      void *data, int size,
                      int nReturned, int startingFrom = 0,
                      long long cursorId = 0
                      );


    /* object reply helper. */
    void replyToQuery(int queryResultFlags,
                      AbstractMessagingPort* p, Message& requestMsg,
                      const BSONObj& responseObj);

    /* helper to do a reply using a DbResponse object */
    void replyToQuery( int queryResultFlags, Message& m, DbResponse& dbresponse, BSONObj obj );

    /**
     * Helper method for setting up a response object.
     *
     * @param queryResultFlags The flags to set to the response object.
     * @param response The object to be used for building the response. The internal buffer of
     *     this object will contain the raw data from resultObj after a successful call.
     * @param resultObj The bson object that contains the reply data.
     */
    void replyToQuery( int queryResultFlags, Message& response, const BSONObj& resultObj );
} // namespace mongo
