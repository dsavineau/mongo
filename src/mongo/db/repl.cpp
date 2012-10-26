// @file repl.cpp

/**
*    Copyright (C) 2008 10gen Inc.
*    Copyright (C) 2013 Tokutek Inc.
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
*/

/* Collections we use:

   local.sources         - indicates what sources we pull from as a "slave", and the last update of each
   local.oplog.$main     - our op log as "master"
   local.dbinfo.<dbname> - no longer used???
   local.pair.startup    - [deprecated] can contain a special value indicating for a pair that we have the master copy.
                           used when replacing other half of the pair which has permanently failed.
   local.pair.sync       - [deprecated] { initialsynccomplete: 1 }
*/

#include "pch.h"

#include <boost/thread/thread.hpp>
#include <string>
#include <vector>

#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/privilege.h"
#include "jsobj.h"
#include "../util/goodies.h"
#include "repl.h"
#include "../util/net/message.h"
#include "../util/background.h"
#include "../client/connpool.h"
#include "commands.h"
#include "cmdline.h"
#include "repl_block.h"
#include "repl/rs.h"
#include "replutil.h"
#include "repl/connections.h"
#include "ops/update.h"
#include "pcrecpp.h"
#include "mongo/db/commands/server_status.h"
#include "mongo/db/instance.h"
#include "mongo/db/queryutil.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/parsed_query.h"

namespace mongo {

    // our config from command line etc.
    ReplSettings replSettings;

    /* if 1 sync() is running */
    volatile int syncing = 0;

    /* "dead" means something really bad happened like replication falling completely out of sync.
       when non-null, we are dead and the string is informational
    */
    const char *replAllDead = 0;

    time_t lastForcedResync = 0;

} // namespace mongo

namespace mongo {

    /* output by the web console */
    const char *replInfo = "";

    bool anyReplEnabled() {
        return theReplSet;
    }

    bool replAuthenticate(DBClientBase *conn);

    void appendReplicationInfo( BSONObjBuilder& result , bool userIsAdmin , int level ) {
        if ( replSet ) {
            if( theReplSet == 0 || theReplSet->state().shunned() ) {
                result.append("ismaster", false);
                result.append("secondary", false);
                result.append("info", ReplSet::startupStatusMsg.get());
                result.append( "isreplicaset" , true );
            }
            else {
                theReplSet->fillIsMaster(result);
            }
            return;
        }
        
        if ( replAllDead ) {
            result.append("ismaster", 0);
            string s = string("dead: ") + replAllDead;
            result.append("info", s);
        }
        else {
            result.appendBool("ismaster", _isMaster() );
        }
        
        if ( level && replSet ) {
            result.append( "info" , "is replica set" );
        }
        else if ( level ) {
            BSONObjBuilder sources( result.subarrayStart( "sources" ) );
            
            int n = 0;
            list<BSONObj> src;
            {
                Client::ReadContext ctx( "local.sources", dbpath, userIsAdmin );
                shared_ptr<Cursor> c = findTableScan("local.sources", BSONObj());
                while ( c->ok() ) {
                    src.push_back(c->current());
                    c->advance();
                }
            }
            
            for( list<BSONObj>::const_iterator i = src.begin(); i != src.end(); i++ ) {
                BSONObj s = *i;
                BSONObjBuilder bb;
                bb.append( s["host"] );
                string sourcename = s["source"].valuestr();
                if ( sourcename != "main" )
                    bb.append( s["source"] );
                {
                    BSONElement e = s["syncedTo"];
                    BSONObjBuilder t( bb.subobjStart( "syncedTo" ) );
                    t.appendDate( "time" , e.timestampTime() );
                    t.append( "inc" , e.timestampInc() );
                    t.done();
                }
                
                if ( level > 1 ) {
                    wassert( !Lock::isLocked() );
                    // note: there is no so-style timeout on this connection; perhaps we should have one.
                    scoped_ptr<ScopedDbConnection> conn( ScopedDbConnection::getInternalScopedDbConnection( s["host"].valuestr() ) );
                    
                    DBClientConnection *cliConn = dynamic_cast< DBClientConnection* >( &conn->conn() );
                    if ( cliConn && replAuthenticate( cliConn ) ) {
                        BSONObj first = conn->get()->findOne( (string)"local.oplog.$" + sourcename,
                                                              Query().sort( BSON( "$natural" << 1 ) ) );
                        BSONObj last = conn->get()->findOne( (string)"local.oplog.$" + sourcename,
                                                             Query().sort( BSON( "$natural" << -1 ) ) );
                        bb.appendDate( "masterFirst" , first["ts"].timestampTime() );
                        bb.appendDate( "masterLast" , last["ts"].timestampTime() );
                        double lag = (double) (last["ts"].timestampTime() - s["syncedTo"].timestampTime());
                        bb.append( "lagSeconds" , lag / 1000 );
                    }
                    conn->done();
                }
                
                sources.append( BSONObjBuilder::numStr( n++ ) , bb.obj() );
            }
            
            sources.done();
        }
    }
    
    class ReplicationInfoServerStatus : public ServerStatusSection {
    public:
        ReplicationInfoServerStatus() : ServerStatusSection( "repl" ){}
        bool includeByDefault() const { return true; }
        bool adminOnly() const { return false; }
        
        BSONObj generateSection( const BSONElement& configElement, bool userIsAdmin ) const {
            if ( ! anyReplEnabled() )
                return BSONObj();
            
            int level = configElement.numberInt();
            
            BSONObjBuilder result;
            appendReplicationInfo( result, userIsAdmin, level );
            return result.obj();
        }
    } replicationInfoServerStatus;


    class CmdIsMaster : public Command {
    public:
        virtual bool requiresAuth() { return false; }
        virtual bool slaveOk() const {
            return true;
        }
        virtual void help( stringstream &help ) const {
            help << "Check if this server is primary for a replica pair/set; also if it is --master or --slave in simple master/slave setups.\n";
            help << "{ isMaster : 1 }";
        }
        virtual LockType locktype() const { return NONE; }
        virtual bool requiresSync() const { return false; }
        virtual bool needsTxn() const { return false; }
        virtual int txnFlags() const { return noTxnFlags(); }
        virtual bool canRunInMultiStmtTxn() const { return true; }
        virtual OpSettings getOpSettings() const { return OpSettings(); }
        virtual void addRequiredPrivileges(const std::string& dbname,
                                           const BSONObj& cmdObj,
                                           std::vector<Privilege>* out) {} // No auth required
        CmdIsMaster() : Command("isMaster", true, "ismaster") { }
        virtual bool run(const string& , BSONObj& cmdObj, int, string& errmsg, BSONObjBuilder& result, bool /*fromRepl*/) {
            /* currently request to arbiter is (somewhat arbitrarily) an ismaster request that is not
               authenticated.
            */
            bool authed = cc().getAuthenticationInfo()->isAuthorizedReads("admin");
            appendReplicationInfo( result , authed , 0 );

            result.appendNumber("maxBsonObjectSize", BSONObjMaxUserSize);
            result.appendNumber("maxMessageSizeBytes", MaxMessageSizeBytes);
            result.appendDate("localTime", jsTime());
            return true;
        }
    } cmdismaster;

    class ReplApplyBatchSize : public ServerParameter {
    public:
        ReplApplyBatchSize()
            : ServerParameter( ServerParameterSet::getGlobal(), "replApplyBatchSize" ),
              _value( 1 ) {
        }

        int get() const { return _value; }

        virtual void append( BSONObjBuilder& b, const string& name ) {
            b.append( name, _value );
        }

        virtual Status set( const BSONElement& newValuElement ) {
            return set( newValuElement.numberInt() );
        }

        virtual Status set( int b ) {
            if( b < 1 || b > 1024 ) {
                return Status( ErrorCodes::BadValue,
                               "replApplyBatchSize has to be >= 1 and < 1024" );
            }

            if ( replSettings.slavedelay != 0 && b > 1 ) {
                return Status( ErrorCodes::BadValue,
                               "can't use a batch size > 1 with slavedelay" );
            }

            _value = b;
            return Status::OK();
        }

        virtual Status setFromString( const string& str ) {
            return set( atoi( str.c_str() ) );
        }

        int _value;

    } replApplyBatchSize;

    void startReplSets(ReplSetCmdline*);
    void startReplication() {
        /* if we are going to be a replica set, we aren't doing other forms of replication. */
        if( !cmdLine._replSet.empty() ) {
            replSet = true;
            setLogTxnOpsForReplication(true);
            setLogTxnToOplog(logTransactionOps);
            setLogTxnRefToOplog(logTransactionOpsRef);
            setLogOpsToOplogRef(logOpsToOplogRef);
            ReplSetCmdline *replSetCmdline = new ReplSetCmdline(cmdLine._replSet);
            boost::thread t( boost::bind( &startReplSets, replSetCmdline) );

            return;
        }
        // we should only be running with replica sets
        // we do not support the old master/slave replication
        else {
            return;
        }
    }

    /** we allow queries to SimpleSlave's */
    void replVerifyReadsOk(const ParsedQuery* pq) {
        if( replSet ) {
            // todo: speed up the secondary case.  as written here there are 2 mutex entries, it
            // can b 1.
            if( isMaster() ) return;
            uassert(13435, "not master and slaveOk=false",
                    !pq || pq->hasOption(QueryOption_SlaveOk) || pq->hasReadPref());
            uassert(13436,
                    "not master or secondary; cannot currently read from this replSet member",
                    theReplSet && theReplSet->isSecondary() );
        }
    }

    OpCounterServerStatusSection replOpCounterServerStatusSection( "opcountersRepl", &replOpCounters );

} // namespace mongo
