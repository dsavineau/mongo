// @file d_concurrency.h

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


// only used by mongod, thus the name ('d')
// (also used by dbtests test binary, which is running mongod test code)

#pragma once

#include "mongo/base/string_data.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/lockstat.h"
#include "mongo/util/concurrency/mutex.h"
#include "mongo/util/concurrency/rwlock.h"

namespace mongo {

    class WrapperForRWLock;
    class LockState;

    class Lock : boost::noncopyable { 
    public:
        enum Nestable { notnestable=0, local, admin };
        static int isLocked();      // true if *anything* is locked (by us)
        static int isReadLocked();  // r or R
        static int somethingWriteLocked(); // w or W
        static bool isW();          // W
        static bool isR();          
        static bool isRW();         // R or W. i.e., we are write-exclusive          
        static bool nested();
        static bool isWriteLocked(const StringData& ns);
        static bool atLeastReadLocked(const StringData& ns); // true if this db is locked
        static void assertAtLeastReadLocked(const StringData& ns);
        static void assertWriteLocked(const StringData& ns);

        static bool dbLevelLockingEnabled(); 
        
        static LockStat* globalLockStat();
        static LockStat* nestableLockStat( Nestable db );

        class ScopedLock;

    public:
        class ScopedLock : boost::noncopyable {
        public:
            virtual ~ScopedLock();

            /** @return micros since we started acquiring */
            long long acquireFinished( LockStat* stat );

            // Accrue elapsed lock time since last we called reset
            void recordTime();
            // Start recording a new period, starting now()
            void resetTime();

        protected:
            explicit ScopedLock( char type ); 

        private:
            void _recordTime( long long micros );
            Timer _timer;
            char _type;      // 'r','w','R','W'
            LockStat* _stat; // the stat for the relevant lock to increment when we're done
        };

        // note that for these classes recursive locking is ok if the recursive locking "makes sense"
        // i.e. you could grab globalread after globalwrite.
        
        class GlobalWrite : public ScopedLock {
            bool noop;
        public:
            // stopGreed is removed and does NOT work
            // timeoutms is only for writelocktry -- deprecated -- do not use
            GlobalWrite(bool stopGreed = false, int timeoutms = -1 ); 
            virtual ~GlobalWrite();
            void downgrade(); // W -> R
            void upgrade();   // caution see notes
        };
        class GlobalRead : public ScopedLock { // recursive is ok
        public:
            bool noop;
        public:
            // timeoutms is only for readlocktry -- deprecated -- do not use
            GlobalRead( int timeoutms = -1 ); 
            virtual ~GlobalRead();
        };

        class DBRead;

        // lock this database. do not shared_lock globally first, that is handledin herein. 
        class DBWrite : public ScopedLock {
            /**
             * flow
             *   1) lockDB
             *      a) lockTop
             *      b) lockNestable or lockOther
             *   2) unlockDB
             */

            void lockTop(LockState&);
            void lockNestable(Nestable db);
            void lockOther(const string& db);
            void lockDB(const string& ns);
            void unlockDB();

        public:
            DBWrite(const StringData& dbOrNs);
            virtual ~DBWrite();

            class UpgradeToExclusive : private boost::noncopyable {
            public:
                UpgradeToExclusive();
                ~UpgradeToExclusive();

                bool gotUpgrade() const { return _gotUpgrade; }
            private:
                bool _gotUpgrade;
            };

            class Downgrade : boost::noncopyable {
                const std::string _ns;
                scoped_ptr<Lock::DBWrite> &_wrlk;
                scoped_ptr<Lock::DBRead> _rdlk;
              public:
                Downgrade(scoped_ptr<Lock::DBWrite> &wrlk)
                        : _ns(wrlk->_what), _wrlk(wrlk) {
                    _wrlk.reset();
                    _rdlk.reset(new Lock::DBRead(_ns));
                }
                ~Downgrade() {
                    _rdlk.reset();
                    _wrlk.reset(new Lock::DBWrite(_ns));
                }
            };

        private:
            bool _locked_w;
            bool _locked_W;
            WrapperForRWLock *_weLocked;
            const string _what;
            bool _nested;
        };

        // lock this database for reading. do not shared_lock globally first, that is handledin herein. 
        class DBRead : public ScopedLock {
            void lockTop(LockState&);
            void lockNestable(Nestable db);
            void lockOther(const string& db);

        public:
            void lockDB(const string& ns);
            void unlockDB();
            DBRead(const StringData& dbOrNs);
            virtual ~DBRead();

        private:
            bool _locked_r;
            WrapperForRWLock *_weLocked;
            string _what;
            bool _nested;
            
        };

    };

    class readlocktry : boost::noncopyable {
        bool _got;
        scoped_ptr<Lock::GlobalRead> _dbrlock;
    public:
        readlocktry( int tryms );
        ~readlocktry();
        bool got() const { return _got; }
    };

    class writelocktry : boost::noncopyable {
        bool _got;
        scoped_ptr<Lock::GlobalWrite> _dbwlock;
    public:
        writelocktry( int tryms );
        ~writelocktry();
        bool got() const { return _got; }
    };

    /** a mutex, but reported in curop() - thus a "high level" (HL) one
        some overhead so we don't use this for everything.  the externalobjsort mutex
        uses this, as it can be held for eons. implementation still needed. */
    class HLMutex : public SimpleMutex {
        LockStat ls;
    public:
        HLMutex(const char *name);
    };


}
