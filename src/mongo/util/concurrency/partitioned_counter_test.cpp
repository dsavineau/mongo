// @file partitioned_counter_test.cpp

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

#include "mongo/unittest/unittest.h"

#include "mongo/util/assert_util.h"
#include "mongo/util/concurrency/partitioned_counter.h"
#include "mongo/util/log.h"
#include "mongo/util/timer.h"

namespace {

    using namespace mongo;

    TEST(PartitionedCounterTest, Create) {
        PartitionedCounter<int> pc1;
        ASSERT_EQUALS(pc1, 0);
        PartitionedCounter<unsigned> pc2;
        ASSERT_EQUALS(pc2.get(), 0);
    }

    TEST(PartitionedCounterTest, CreateWithArg) {
        PartitionedCounter<int> pc3(4);
        ASSERT_EQUALS(pc3.get(), 4);
    }

    TEST(PartitionedCounterTest, Increment) {
        PartitionedCounter<int> pc;
        int i = 0;
        while (i < 10) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(++pc, ++i);
        }
        while (i < 20) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc += 1, ++i);
        }
        while (i < 30) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc.inc(), ++i);
        }
    }

    TEST(PartitionedCounterTest, IncrementWithArg) {
        PartitionedCounter<int> pc;
        int i = 0;
        while (i < 30) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc.inc(3), i += 3);
        }
        while (i < 60) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc += 3, i += 3);
        }
    }

    TEST(PartitionedCounterTest, Decrement) {
        PartitionedCounter<int> pc(100);
        int i = 100;
        while (i > 90) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(--pc, --i);
        }
        while (i > 80) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc -= 1, --i);
        }
        while (i > 70) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc.dec(), --i);
        }
    }

    TEST(PartitionedCounterTest, DecrementWithArg) {
        PartitionedCounter<int> pc;
        int i = 0;
        while (i > 70) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc.dec(3), i -= 3);
        }
        while (i > 40) {
            ASSERT_EQUALS(pc, i);
            ASSERT_EQUALS(pc -= 3, i -= 3);
        }
    }

    TEST(PartitionedCounterTest, DecrementUnsignedUnderflow) {
        PartitionedCounter<unsigned> pc(3);
        ASSERT_THROWS(pc -= 4, MsgAssertionException);
    }

} // namespace
