// count.cpp

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

#include "mongo/db/ops/count.h"
#include "mongo/db/client.h"
#include "mongo/db/clientcursor.h"
#include "mongo/db/namespace_details.h"
#include "mongo/db/queryutil.h"
#include "mongo/db/queryoptimizercursor.h"
#include "mongo/client/dbclientinterface.h"

namespace mongo {

    long long runCount( const char *ns, const BSONObj &cmd, string &err, int &errCode ) {
        Client::ReadContext ctx(ns);
        NamespaceDetails *d = nsdetails( ns );
        if ( !d ) {
            err = "ns missing";
            return -1;
        }
        BSONObj query = cmd.getObjectField("query");

        // count of all objects
        if ( query.isEmpty() ) {
            // TODO: TokuMX: call this with in-memory stats once we maintain them
            //return applySkipLimit( d->stats.nrecords , cmd );
        }

        long long count = 0;
        long long skip = cmd["skip"].numberLong();
        long long limit = cmd["limit"].numberLong();

        if ( limit < 0 ) {
            limit  = -limit;
        }

<<<<<<< HEAD
        OpSettings settings;
        settings.setBulkFetch(true);
        settings.setQueryCursorMode(DEFAULT_LOCK_CURSOR);
        cc().setOpSettings(settings);

        Lock::assertAtLeastReadLocked(ns);
        Client::Transaction transaction(DB_TXN_SNAPSHOT | DB_TXN_READ_ONLY);
        try {
            shared_ptr<Cursor> cursor =
                    getOptimizedCursor( ns, query, BSONObj(), QueryPlanSelectionPolicy::any(),
                                        // Avoid using a Matcher when a Cursor can
                                        // exactly match the query using a
                                        // FieldRangeVector.  See SERVER-1752.
                                        false /* requestMatcher */ );
            for ( ; cursor->ok() ; cursor->advance() ) {
                if ( cursor->currentMatches() && !cursor->getsetdup( cursor->currPK() ) ) {
=======
        shared_ptr<Cursor> cursor =
                NamespaceDetailsTransient::getCursor( ns,
                                                      query,
                                                      BSONObj(),
                                                      QueryPlanSelectionPolicy::any(),
                                                      // Avoid using a Matcher when a Cursor can
                                                      // exactly match the query using a
                                                      // FieldRangeVector.  See SERVER-1752.
                                                      false /* requestMatcher */ );
        ClientCursor::Holder ccPointer;
        ElapsedTracker timeToStartYielding( 256, 20 );
        try {
            while( cursor->ok() ) {
                if ( !ccPointer ) {
                    if ( timeToStartYielding.intervalHasElapsed() ) {
                        // Lazily construct a ClientCursor, avoiding a performance regression when scanning a very
                        // small number of documents.
                        ccPointer.reset( new ClientCursor( QueryOption_NoCursorTimeout, cursor, ns ) );
                    }
                }
                else if ( !ccPointer->yieldSometimes( ClientCursor::MaybeCovered ) ||
                         !cursor->ok() ) {
                    break;
                }
                
                if ( cursor->currentMatches() && !cursor->getsetdup( cursor->currLoc() ) ) {
                    
>>>>>>> c0355cc... SERVER-1752 Improve performance of simple counts by avoiding use of a matcher when an optimal btree cursor can filter results internally.
                    if ( skip > 0 ) {
                        --skip;
                    }
                    else {
                        ++count;
                        if ( limit > 0 && count >= limit ) {
                            break;
                        }
                    }
                }
            }
            transaction.commit();
            return count;
        }
        catch ( const DBException &e ) {
            err = e.toString();
            errCode = e.getCode();
            count = -2;
        }
        catch ( const std::exception &e ) {
            err = e.what();
            errCode = 0;
            count = -2;
        }
        if ( count != -2 ) { // keeping the magical -2 return value for legacy reasons...
            transaction.commit();
        } else {
            // Historically we have returned zero in many count assertion cases - see SERVER-2291.
            log() << "Count with ns: " << ns << " and query: " << query
                  << " failed with exception: " << err << " code: " << errCode
                  << endl;
        }
        return count;
    }

} // namespace mongo
