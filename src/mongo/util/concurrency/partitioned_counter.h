// @file partitioned_counter.h

/*    Copyright (C) 2013 Tokutek Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include "mongo/pch.h"

#include <limits>
#include <list>
#include <boost/thread/tss.hpp>

#include "mongo/util/assert_util.h"
#include "mongo/util/concurrency/mutex.h"

namespace mongo {

    using boost::thread_specific_ptr;
    using std::list;

    extern SimpleMutex pcMutex;

    /**
     * PartitionedCounter is a number that can be incremented, decremented, and read.
     *
     * It is assumed that increments and decrements are frequent and concurrent, whereas reads are
     * infrequent.  Therefore, the key is to do increments and decrements without involving a memory
     * location shared between threads.
     *
     * The original implementation is in ft-index under util/partitioned_counter.{h,cc}.  This is a
     * C++ implementation that should be friendlier to mongo code.  In addition, it has the
     * following differences:
     *
     *   - Signed or unsigned types are supported, because it's templated.
     *   - Decrement is supported, and if Value is an unsigned type, decrements check for underflow.
     *   - This implementation is sloppier about memory management.  When a thread dies, it does not
     *     clean up the pointer to itself in the owning PartitionedCounter, rather that
     *     PartitionedCounter will notice that the pointer is NULL and lazily clean up.  This is a
     *     big complexity win.
     *   - There is no global cleanup like partitioned_counters_destroy, if there are global objects
     *     they get destructed just like everything else.
     */
    template<typename Value>
    class PartitionedCounter : boost::noncopyable {
      public:
        PartitionedCounter();
        ~PartitionedCounter();

        PartitionedCounter& inc(Value);
        PartitionedCounter& dec(Value);

        Value get() const;

        // convenience API

        operator Value() const { return get(); }

        // prefix
        PartitionedCounter& operator++(int) { return inc(1); }
        PartitionedCounter& operator--(int) { return dec(1); }

        // maybe TODO: postfix (can't do because we'd need to copy the partitioned counter)
        //Value operator++(Value) { Value x = get(); inc(1); return x; }
        //Value operator--(Value) { Value x = get(); dec(1); return x; }

        PartitionedCounter& operator+=(Value x) { return inc(x); }
        PartitionedCounter& operator-=(Value x) { return dec(x); }

      private:
        class ThreadState : boost::noncopyable {
            PartitionedCounter *_pc;
            Value _sum;
          public:
            ThreadState(PartitionedCounter *);
            ~ThreadState();
            friend class PartitionedCounter;
        };
        friend class ThreadState;
        typedef thread_specific_ptr<ThreadState> ThreadStatePtr;

        ThreadState& ts();

        Value _sumOfDead;
        ThreadStatePtr _ts;
        mutable list<ThreadStatePtr *> _threadStates;
        typedef typename list<ThreadStatePtr *>::iterator states_iterator;
    };

    template<typename Value>
    PartitionedCounter<Value>::ThreadState::ThreadState(PartitionedCounter *pc) : _pc(pc), _sum(0) {}

    template<typename Value>
    PartitionedCounter<Value>::ThreadState::~ThreadState() {
        if (_pc != NULL) {
            SimpleMutex::scoped_lock lk(pcMutex);
            _pc->_sumOfDead += _sum;
        }
    }

    template<typename Value>
    PartitionedCounter<Value>::PartitionedCounter() : _sumOfDead(0) {}

    template<typename Value>
    PartitionedCounter<Value>::~PartitionedCounter() {
        SimpleMutex::scoped_lock lk(pcMutex);
        for (states_iterator it = _threadStates.begin(); it != _threadStates.end(); ++it) {
            ThreadStatePtr *tspp = *it;
            if (tspp->get() != NULL) {
                // Prevent recursive lock, we don't care about incrementing _sumOfDead because this
                // pc is dying anyway, but we do need to delete all the corresponding ThreadStates.
                (*tspp)->_pc = NULL;
                tspp->reset();
            }
        }
    }

    template<typename Value>
    PartitionedCounter<Value>& PartitionedCounter<Value>::inc(Value x) {
        ts()._sum += x;
        return *this;
    }

    template<typename Value>
    PartitionedCounter<Value>& PartitionedCounter<Value>::dec(Value x) {
        if (!std::numeric_limits<Value>::is_signed) {
            massert(17019, "cannot decrement partitioned counter below zero", ts()._sum > x);
        }
        ts()._sum -= x;
        return *this;
    }

    template<typename Value>
    Value PartitionedCounter<Value>::get() const {
        SimpleMutex::scoped_lock lk(pcMutex);
        Value sum = _sumOfDead;
        for (states_iterator it = _threadStates.begin(); it != _threadStates.end(); ) {
            ThreadStatePtr *tspp = *it;
            if (tspp->get() == NULL) {
                // opportunistically clean up the list
                it = _threadStates.erase(it);
            } else {
                sum += (*tspp)->_sum;
                ++it;
            }
        }
        return sum;
    }

    template<typename Value>
    typename PartitionedCounter<Value>::ThreadState& PartitionedCounter<Value>::ts() {
        if (_ts.get() == NULL) {
            _ts.reset(new ThreadState(this));
            SimpleMutex::scoped_lock lk(pcMutex);
            _threadStates.push_back(&_ts);
        }
        return *_ts;
    }

} // namespace mongo
