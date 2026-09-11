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

#ifndef AETHER_OBJECTS_TESTS_TEST_OBJECT_SYSTEM_OBJECTS_LIFECYCLE_H_
#define AETHER_OBJECTS_TESTS_TEST_OBJECT_SYSTEM_OBJECTS_LIFECYCLE_H_

#include <vector>

#include "aether-objects/obj/obj.h"

namespace ae {
class LifecycleObject : public Obj {
  AE_OBJECT(LifecycleObject, Obj, 0)

  LifecycleObject() = default;

  void Loaded() {
    ++loaded_count;
    runtime_value = persisted_value + 1;
  }

  void Saved() {
    ++saved_count;
    persisted_value = saved_mutation_value;
  }

 public:
  explicit LifecycleObject(ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(persisted_value))

  template <typename Dnv>
  void Load(Version<0>, Dnv& dnv) {
    dnv(persisted_value);
  }

  static void ResetCallbacks() {
    loaded_count = 0;
    saved_count = 0;
  }

  static inline int loaded_count{};
  static inline int saved_count{};
  static constexpr int saved_mutation_value = 99;
  int persisted_value{};
  int runtime_value{};
};

class LifecycleBase : public Obj {
  AE_OBJECT(LifecycleBase, Obj, 0)

 public:
  void Loaded() { calls.push_back(kBaseLoaded); }
  void Saved() { calls.push_back(kBaseSaved); }

 protected:
  LifecycleBase() = default;

 public:
  explicit LifecycleBase(ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  static void ResetCalls() { calls.clear(); }

  static constexpr int kBaseLoaded = 1;
  static constexpr int kDerivedLoaded = 2;
  static constexpr int kBaseSaved = 3;
  static constexpr int kDerivedSaved = 4;
  static inline std::vector<int> calls;
};

class LifecycleDerivedOnly : public LifecycleBase {
  AE_OBJECT(LifecycleDerivedOnly, LifecycleBase, 0)

  LifecycleDerivedOnly() = default;

  void Loaded() { calls.push_back(kDerivedLoaded); }
  void Saved() { calls.push_back(kDerivedSaved); }

 public:
  explicit LifecycleDerivedOnly(ObjProp prop) : LifecycleBase{prop} {}

  AE_OBJECT_REFLECT()
};

class LifecycleDerivedChained : public LifecycleBase {
  AE_OBJECT(LifecycleDerivedChained, LifecycleBase, 0)

  LifecycleDerivedChained() = default;

  void Loaded() {
    LifecycleBase::Loaded();
    calls.push_back(kDerivedLoaded);
  }

  void Saved() {
    LifecycleBase::Saved();
    calls.push_back(kDerivedSaved);
  }

 public:
  explicit LifecycleDerivedChained(ObjProp prop) : LifecycleBase{prop} {}

  AE_OBJECT_REFLECT()
};

class LifecycleDerivedInherited : public LifecycleBase {
  AE_OBJECT(LifecycleDerivedInherited, LifecycleBase, 0)

  LifecycleDerivedInherited() = default;

 public:
  explicit LifecycleDerivedInherited(ObjProp prop) : LifecycleBase{prop} {}

  AE_OBJECT_REFLECT()
};

class LifecycleGraphObject : public Obj {
  AE_OBJECT(LifecycleGraphObject, Obj, 0)

  LifecycleGraphObject() = default;

  void Saved() {
    ++saved_count;
    saved_ids.push_back(obj_id.id());
    value = saved_mutation_value;
  }

  void Loaded() { ++loaded_count; }

 public:
  explicit LifecycleGraphObject(ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(first), AE_MMBR(second), AE_MMBR(value))

  static void ResetCallbacks() {
    loaded_count = 0;
    saved_count = 0;
    saved_ids.clear();
  }

  static inline int loaded_count{};
  static inline int saved_count{};
  static inline std::vector<ObjId::Type> saved_ids{};
  static constexpr int saved_mutation_value = 99;
  ObjPtr<LifecycleGraphObject> first;
  ObjPtr<LifecycleGraphObject> second;
  int value{};
};

class LifecycleNoexceptObject : public Obj {
  AE_OBJECT(LifecycleNoexceptObject, Obj, 0)

  LifecycleNoexceptObject() = default;

  void Loaded() noexcept { ++loaded_count; }
  void Loaded(int) {}
  void Saved() noexcept { ++saved_count; }
  void Saved(int) {}

 public:
  explicit LifecycleNoexceptObject(ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  static void ResetCallbacks() {
    loaded_count = 0;
    saved_count = 0;
  }

  static inline int loaded_count{};
  static inline int saved_count{};
  int value{};
};
}  // namespace ae

#endif  // AETHER_OBJECTS_TESTS_TEST_OBJECT_SYSTEM_OBJECTS_LIFECYCLE_H_
