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

#include "aether-objects/obj/obj_id.h"

#include <limits>
#include <mutex>
#include <random>

namespace ae {
ObjId ObjId::GenerateUnique() {
  // One process-wide identity source shared by every Domain. Applications may
  // create objects from several threads, so the engine and the distribution
  // state must be advanced under a lock: std::mt19937 and
  // std::uniform_int_distribution are both mutated by operator().
  static std::mutex mutex;
  static std::random_device dev;
  static std::mt19937 rng(dev());
  static std::uniform_int_distribution<std::mt19937::result_type> dist6(
      10000, std::numeric_limits<Type>::max());
  std::lock_guard<std::mutex> lock{mutex};
  // Probabilistic uniqueness in a 32-bit space: the mutex makes the engine
  // advance correctly under concurrency; it does not turn the draw into a
  // collision-free allocator.
  return ObjId{static_cast<ObjId::Type>(dist6(rng))};
}
}  // namespace ae
