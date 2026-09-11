/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unity.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <unordered_set>
#include <vector>

#include "aether-objects/obj/obj_id.h"

namespace ae::test_obj_id {

namespace test_obj_id_internal {

constexpr std::size_t kThreadCount = 8;
constexpr std::size_t kIdsPerThread = 128;
constexpr ObjId::Type kLowestGeneratedId = 10000;

}  // namespace test_obj_id_internal

void test_GenerateUniqueSingleThreadIsValid() {
  for (std::size_t i = 0; i < test_obj_id_internal::kIdsPerThread; ++i) {
    auto const id = ObjId::GenerateUnique();
    TEST_ASSERT_TRUE(id.is_valid());
    TEST_ASSERT_TRUE(id.id() >= test_obj_id_internal::kLowestGeneratedId);
  }
}

// The generator state is process-wide: a shared std::mt19937 and a shared
// distribution are both advanced by every call. Unsynchronized concurrent
// calls read the same engine word before either advances it, which shows up
// as repeated ids. Threads start together so the calls actually overlap.
void test_GenerateUniqueIsThreadSafe() {
  std::atomic<bool> start{false};
  std::vector<std::vector<ObjId>> per_thread(
      test_obj_id_internal::kThreadCount);
  std::vector<std::thread> threads;
  threads.reserve(test_obj_id_internal::kThreadCount);

  for (std::size_t t = 0; t < test_obj_id_internal::kThreadCount; ++t) {
    auto& sink = per_thread[t];
    sink.reserve(test_obj_id_internal::kIdsPerThread);
    threads.emplace_back([&start, &sink] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (std::size_t i = 0; i < test_obj_id_internal::kIdsPerThread; ++i) {
        sink.push_back(ObjId::GenerateUnique());
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }

  std::unordered_set<ObjId::Type> seen;
  std::size_t total = 0;
  for (auto const& sink : per_thread) {
    TEST_ASSERT_TRUE(sink.size() == test_obj_id_internal::kIdsPerThread);
    for (ObjId const& id : sink) {
      TEST_ASSERT_TRUE(id.is_valid());
      TEST_ASSERT_TRUE(id.id() >= test_obj_id_internal::kLowestGeneratedId);
      seen.insert(id.id());
      ++total;
    }
  }
  TEST_ASSERT_TRUE(total == test_obj_id_internal::kThreadCount *
                                test_obj_id_internal::kIdsPerThread);
  TEST_ASSERT_TRUE(seen.size() == total);
}

}  // namespace ae::test_obj_id

int run_test_obj_id() {
  using namespace ae::test_obj_id;  // NOLINT: Unity suite entry needs the names
  UNITY_BEGIN();
  RUN_TEST(test_GenerateUniqueSingleThreadIsValid);
  RUN_TEST(test_GenerateUniqueIsThreadSafe);
  return UNITY_END();
}
