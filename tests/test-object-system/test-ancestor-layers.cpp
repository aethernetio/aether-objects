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

#include <vector>

#include "aether-miscpp/serialization/serialization.h"
#include "aether-objects/obj/domain.h"
#include "map_domain_storage.h"
#include "objects/ancestor_layers.h"

namespace ae::test_ancestor_layers_internal {
void NoteLayer(int layer);
}

namespace ae::seri {
template <Archive A>
struct Serializer<A, AncProbe> {
  SeriResult Seri(A& archive, Meta<AncProbe const> meta) const {
    return archive.Save(Meta{meta.value.layer});
  }

  SeriResult Deseri(A& archive, Meta<AncProbe> meta) const {
    auto result = archive.Load(Meta{meta.value.layer});
    if (result) {
      test_ancestor_layers_internal::NoteLayer(meta.value.layer);
    }
    return result;
  }
};
}  // namespace ae::seri

namespace ae::test_ancestor_layers_internal {

inline std::vector<int> layer_order;

inline void NoteLayer(int layer) { layer_order.push_back(layer); }

inline void ResetLayers() { layer_order.clear(); }

inline int CountLayer(int layer) {
  int n = 0;
  for (int value : layer_order) {
    if (value == layer) {
      ++n;
    }
  }
  return n;
}

inline void DropClass(MapDomainStorage& storage, std::uint32_t class_id) {
  for (auto& [_, classes] : storage.map) {
    classes.erase(class_id);
  }
}

inline void SaveGraph(MapDomainStorage& storage, int base, int middle,
                      int stored) {
  Domain domain{storage};
  auto root = AncRuntime::ptr::Create(CreateWith{domain}.with_id(1));
  auto peer = AncPeer::ptr::Create(CreateWith{domain}.with_id(2));
  root->base_value = base;
  root->middle_value = middle;
  root->stored_value = stored;
  root->runtime_only = 99;
  root->peer = peer;
  peer->back = root;
  root.Save();
}

void test_StoredBaseRuntimeDerived() {
  ResetLayers();
  MapDomainStorage storage;
  SaveGraph(storage, 11, 22, 33);
  DropClass(storage, AncMiddle::kClassId);
  DropClass(storage, AncStored::kClassId);
  DropClass(storage, AncRuntime::kClassId);

  Domain domain{storage};
  auto root = AncRuntime::ptr::Declare(CreateWith{domain}.with_id(1));
  TEST_ASSERT(root.Load());
  TEST_ASSERT_EQUAL(AncRuntime::kClassId, root->GetClassId());
  TEST_ASSERT_EQUAL(11, root->base_value);
  TEST_ASSERT_EQUAL(0, root->middle_value);
  TEST_ASSERT_EQUAL(0, root->stored_value);
  TEST_ASSERT_EQUAL(7, root->runtime_only);
  TEST_ASSERT_EQUAL(1, CountLayer(1));
}

void test_MultipleStoredAncestors() {
  ResetLayers();
  MapDomainStorage storage;
  SaveGraph(storage, 11, 22, 33);
  DropClass(storage, AncRuntime::kClassId);

  Domain domain{storage};
  auto root = AncRuntime::ptr::Declare(CreateWith{domain}.with_id(1));
  TEST_ASSERT(root.Load());
  TEST_ASSERT_EQUAL(AncRuntime::kClassId, root->GetClassId());
  TEST_ASSERT_EQUAL(11, root->base_value);
  TEST_ASSERT_EQUAL(22, root->middle_value);
  TEST_ASSERT_EQUAL(33, root->stored_value);
  TEST_ASSERT_EQUAL(7, root->runtime_only);
  TEST_ASSERT_EQUAL(1, CountLayer(1));
  TEST_ASSERT_EQUAL(1, CountLayer(2));
  TEST_ASSERT_EQUAL(1, CountLayer(3));
}

void test_LoadOrderWithoutMiddleLayer() {
  ResetLayers();
  MapDomainStorage storage;
  SaveGraph(storage, 11, 22, 33);
  DropClass(storage, AncMiddle::kClassId);
  DropClass(storage, AncRuntime::kClassId);

  Domain domain{storage};
  auto root = AncRuntime::ptr::Declare(CreateWith{domain}.with_id(1));
  TEST_ASSERT(root.Load());
  TEST_ASSERT_EQUAL(11, root->base_value);
  TEST_ASSERT_EQUAL(33, root->stored_value);
  TEST_ASSERT_EQUAL(2, static_cast<int>(layer_order.size()));
  TEST_ASSERT_EQUAL(1, layer_order[0]);
  TEST_ASSERT_EQUAL(3, layer_order[1]);
}

void test_ExactStoredClass() {
  ResetLayers();
  MapDomainStorage storage;
  SaveGraph(storage, 11, 22, 33);

  Domain domain{storage};
  auto root = AncRuntime::ptr::Declare(CreateWith{domain}.with_id(1));
  TEST_ASSERT(root.Load());
  TEST_ASSERT_EQUAL(AncRuntime::kClassId, root->GetClassId());
  TEST_ASSERT_EQUAL(11, root->base_value);
  TEST_ASSERT_EQUAL(22, root->middle_value);
  TEST_ASSERT_EQUAL(33, root->stored_value);
  TEST_ASSERT_EQUAL(99, root->runtime_only);
  TEST_ASSERT_EQUAL(1, CountLayer(1));
  TEST_ASSERT_EQUAL(1, CountLayer(2));
  TEST_ASSERT_EQUAL(1, CountLayer(3));
}

void test_UnknownMoreDerivedClass() {
  ResetLayers();
  MapDomainStorage storage;
  SaveGraph(storage, 41, 22, 33);
  DropClass(storage, AncMiddle::kClassId);
  DropClass(storage, AncStored::kClassId);
  DropClass(storage, AncRuntime::kClassId);
  storage.SaveData({ObjId{1}, 0xDeadBeefu, 0}, ObjectData{1, 2, 3});

  Domain domain{storage};
  auto root = AncRuntime::ptr::Declare(CreateWith{domain}.with_id(1));
  TEST_ASSERT(root.Load());
  TEST_ASSERT_EQUAL(AncRuntime::kClassId, root->GetClassId());
  TEST_ASSERT_EQUAL(41, root->base_value);
  TEST_ASSERT_EQUAL(1, CountLayer(1));
}

void test_ReferenceCycleOneInstance() {
  ResetLayers();
  MapDomainStorage storage;
  SaveGraph(storage, 11, 22, 33);
  DropClass(storage, AncRuntime::kClassId);

  Domain domain{storage};
  auto root = AncRuntime::ptr::Declare(CreateWith{domain}.with_id(1));
  TEST_ASSERT(root.Load());
  TEST_ASSERT(root->peer);
  auto peer = root->peer.Load();
  TEST_ASSERT(peer);
  TEST_ASSERT(peer->back);
  auto back = peer->back.Load();
  TEST_ASSERT(back);
  TEST_ASSERT_EQUAL_PTR(root.Load().get(), back.get());
  TEST_ASSERT_EQUAL_PTR(domain.Find(ObjId{1}).get(), root.Load().get());
  TEST_ASSERT_EQUAL_PTR(domain.Find(ObjId{2}).get(), peer.get());
}

}  // namespace ae::test_ancestor_layers_internal

int run_test_ancestor_layers() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ancestor_layers_internal::test_StoredBaseRuntimeDerived);
  RUN_TEST(ae::test_ancestor_layers_internal::test_MultipleStoredAncestors);
  RUN_TEST(ae::test_ancestor_layers_internal::test_LoadOrderWithoutMiddleLayer);
  RUN_TEST(ae::test_ancestor_layers_internal::test_ExactStoredClass);
  RUN_TEST(ae::test_ancestor_layers_internal::test_UnknownMoreDerivedClass);
  RUN_TEST(ae::test_ancestor_layers_internal::test_ReferenceCycleOneInstance);
  return UNITY_END();
}
