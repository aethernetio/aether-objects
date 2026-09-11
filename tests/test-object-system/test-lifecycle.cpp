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

#include <algorithm>
#include <memory>

#include <unity.h>

#include "aether-objects/obj/domain.h"
#include "map_domain_storage.h"
#include "objects/lifecycle.h"

namespace ae::test_lifecycle {
namespace {
inline constexpr char kNestedWriteErrorMessage[] = "Nested write failed";
inline constexpr seri::SeriError kNestedWriteError{
    .error_code = -1, .message = kNestedWriteErrorMessage};

class FailingWriter final : public IDomainStorageWriter {
 public:
  explicit FailingWriter(seri::SeriError error = seri::write_error)
      : error_{error} {}

  seri::SeriResult Write(seri::SizeWriteTag) override { return Error{error_}; }

  seri::SeriResult Write(seri::DataWriteTag) override { return Error{error_}; }

 private:
  seri::SeriError error_;
};

class FailingSaveStorage final : public MapDomainStorage {
 public:
  std::unique_ptr<IDomainStorageWriter> Store(DomainQuery const&) override {
    return std::make_unique<FailingWriter>();
  }
};

class FailingNestedSaveStorage final : public MapDomainStorage {
 public:
  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& query) override {
    if (query.id == ObjId{2}) {
      return std::make_unique<FailingWriter>(kNestedWriteError);
    }
    return MapDomainStorage::Store(query);
  }
};

// Verifies successful saves and loads notify lifecycle callbacks immediately.
void test_SuccessfulCallbacksAreSynchronous() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  LifecycleObject::ResetCallbacks();

  auto object = LifecycleObject::ptr::Create(CreateWith{domain}.with_id(1));
  object->persisted_value = 7;
  object.Save();

  TEST_ASSERT_EQUAL(1, LifecycleObject::saved_count);
  TEST_ASSERT_EQUAL(LifecycleObject::saved_mutation_value,
                    object->persisted_value);

  LifecycleObject::ResetCallbacks();
  Domain load_domain{domain_storage};
  auto loaded =
      LifecycleObject::ptr::Declare(CreateWith{load_domain}.with_id(1));
  loaded.Load();

  TEST_ASSERT_EQUAL(1, LifecycleObject::loaded_count);
  TEST_ASSERT_EQUAL(7, loaded->persisted_value);
  TEST_ASSERT_EQUAL(8, loaded->runtime_value);
  loaded.Load();
  TEST_ASSERT_EQUAL(1, LifecycleObject::loaded_count);
}

// Verifies failed loads, failed saves, and empty pointers do not notify
// callbacks.
void test_FailedOperationsDoNotNotify() {
  auto domain_storage = MapDomainStorage{};
  Domain save_domain{domain_storage};
  auto saved = LifecycleObject::ptr::Create(CreateWith{save_domain}.with_id(1));
  saved->persisted_value = 7;
  saved.Save();
  domain_storage.map[1][LifecycleObject::kClassId][0]->clear();

  LifecycleObject::ResetCallbacks();
  Domain load_domain{domain_storage};
  auto loaded =
      LifecycleObject::ptr::Declare(CreateWith{load_domain}.with_id(1));
  loaded.Load();
  TEST_ASSERT_EQUAL(0, LifecycleObject::loaded_count);

  auto failing_domain_storage = FailingSaveStorage{};
  Domain failing_save_domain{failing_domain_storage};
  auto to_save =
      LifecycleObject::ptr::Create(CreateWith{failing_save_domain}.with_id(2));
  LifecycleObject::ResetCallbacks();
  to_save.Save();
  TEST_ASSERT_EQUAL(0, LifecycleObject::saved_count);

  LifecycleObject::ptr empty;
  empty.Save();
  TEST_ASSERT_EQUAL(0, LifecycleObject::saved_count);
}

// Verifies absent and removed records fail without invoking load callbacks.
void test_AbsentOrRemovedDataDoesNotNotify() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};

  LifecycleObject::ResetCallbacks();
  auto empty = LifecycleObject::ptr::Declare(CreateWith{domain}.with_id(1));
  empty.Load();
  TEST_ASSERT_EQUAL(0, LifecycleObject::loaded_count);

  auto empty_graph = DomainGraph{&domain};
  auto empty_object =
      LifecycleObject::ptr::Create(CreateWith{domain}.with_id(3));
  auto empty_result = empty_graph.Load(*empty_object, ObjId{3});
  TEST_ASSERT_TRUE(empty_result.has_value());
  TEST_ASSERT_FALSE(empty_result->IsOk());
  TEST_ASSERT_EQUAL(domain_internal::missing_version.error_code,
                    empty_result->error().error_code);

  auto saved = LifecycleObject::ptr::Create(CreateWith{domain}.with_id(2));
  saved.Save();
  domain_storage.Remove(ObjId{2});

  LifecycleObject::ResetCallbacks();
  Domain load_domain{domain_storage};
  auto removed =
      LifecycleObject::ptr::Declare(CreateWith{load_domain}.with_id(2));
  removed.Load();
  TEST_ASSERT_EQUAL(0, LifecycleObject::loaded_count);

  auto removed_graph = DomainGraph{&domain};
  auto removed_result = removed_graph.Load(*saved, ObjId{2});
  TEST_ASSERT_TRUE(removed_result.has_value());
  TEST_ASSERT_FALSE(removed_result->IsOk());
  TEST_ASSERT_EQUAL(seri::read_error.error_code,
                    removed_result->error().error_code);
}

// Verifies derived callbacks invoke base callbacks only when explicitly
// chained.
void test_DerivedCallbacksRequireExplicitBaseChaining() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};

  LifecycleBase::ResetCalls();
  auto derived_only =
      LifecycleDerivedOnly::ptr::Create(CreateWith{domain}.with_id(1));
  derived_only.Save();
  TEST_ASSERT_EQUAL(1, LifecycleBase::calls.size());
  TEST_ASSERT_EQUAL(LifecycleBase::kDerivedSaved, LifecycleBase::calls[0]);

  LifecycleBase::ResetCalls();
  auto derived_chained =
      LifecycleDerivedChained::ptr::Create(CreateWith{domain}.with_id(2));
  derived_chained.Save();
  TEST_ASSERT_EQUAL(2, LifecycleBase::calls.size());
  TEST_ASSERT_EQUAL(LifecycleBase::kBaseSaved, LifecycleBase::calls[0]);
  TEST_ASSERT_EQUAL(LifecycleBase::kDerivedSaved, LifecycleBase::calls[1]);

  LifecycleBase::ResetCalls();
  Domain load_domain{domain_storage};
  auto loaded_only =
      LifecycleDerivedOnly::ptr::Declare(CreateWith{load_domain}.with_id(1));
  loaded_only.Load();
  TEST_ASSERT_EQUAL(1, LifecycleBase::calls.size());
  TEST_ASSERT_EQUAL(LifecycleBase::kDerivedLoaded, LifecycleBase::calls[0]);

  LifecycleBase::ResetCalls();
  auto loaded_chained =
      LifecycleDerivedChained::ptr::Declare(CreateWith{load_domain}.with_id(2));
  loaded_chained.Load();
  TEST_ASSERT_EQUAL(2, LifecycleBase::calls.size());
  TEST_ASSERT_EQUAL(LifecycleBase::kBaseLoaded, LifecycleBase::calls[0]);
  TEST_ASSERT_EQUAL(LifecycleBase::kDerivedLoaded, LifecycleBase::calls[1]);
}

// Verifies inherited lifecycle callbacks are invoked for derived objects.
void test_InheritedCallbacksAreNotified() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};

  LifecycleBase::ResetCalls();
  auto derived =
      LifecycleDerivedInherited::ptr::Create(CreateWith{domain}.with_id(1));
  derived.Save();
  TEST_ASSERT_EQUAL(1, LifecycleBase::calls.size());
  TEST_ASSERT_EQUAL(LifecycleBase::kBaseSaved, LifecycleBase::calls[0]);

  LifecycleBase::ResetCalls();
  Domain load_domain{domain_storage};
  auto loaded = LifecycleDerivedInherited::ptr::Declare(
      CreateWith{load_domain}.with_id(1));
  loaded.Load();
  TEST_ASSERT_EQUAL(1, LifecycleBase::calls.size());
  TEST_ASSERT_EQUAL(LifecycleBase::kBaseLoaded, LifecycleBase::calls[0]);
}

// Verifies cyclic shared graphs serialize once per object and notify once.
void test_CyclicSharedSerializationReportsSuccessAndNotifiesOnce() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  LifecycleGraphObject::ResetCallbacks();

  auto first = LifecycleGraphObject::ptr::Create(CreateWith{domain}.with_id(1));
  auto second =
      LifecycleGraphObject::ptr::Create(CreateWith{domain}.with_id(2));
  first->value = 7;
  second->value = 8;
  first->first = second;
  first->second = second;
  second->first = first;
  auto graph = DomainGraph{&domain};
  Ptr<Obj> loaded_first = first.Load();
  TEST_ASSERT_TRUE(graph.SaveRoot(loaded_first, ObjId{1}).IsOk());

  TEST_ASSERT_EQUAL(2, LifecycleGraphObject::saved_count);
  TEST_ASSERT_EQUAL(
      1, std::count(LifecycleGraphObject::saved_ids.begin(),
                    LifecycleGraphObject::saved_ids.end(), ObjId::Type{1}));
  TEST_ASSERT_EQUAL(
      1, std::count(LifecycleGraphObject::saved_ids.begin(),
                    LifecycleGraphObject::saved_ids.end(), ObjId::Type{2}));

  auto repeated_save = graph.Save(*first, ObjId{1});
  TEST_ASSERT_FALSE(repeated_save.has_value());
  TEST_ASSERT_EQUAL(2, LifecycleGraphObject::saved_count);

  Domain load_domain{domain_storage};
  auto loaded =
      LifecycleGraphObject::ptr::Declare(CreateWith{load_domain}.with_id(1));
  loaded.Load();
  TEST_ASSERT_EQUAL(7, loaded->value);
  TEST_ASSERT_EQUAL(2, LifecycleGraphObject::loaded_count);

  auto repeated_load = graph.Load(*first, ObjId{1});
  TEST_ASSERT_FALSE(repeated_load.has_value());
}

// Verifies nested save failures propagate and suppress all save notifications.
void test_NestedSaveFailurePropagatesAndDoesNotNotify() {
  auto domain_storage = FailingNestedSaveStorage{};
  Domain domain{domain_storage};
  LifecycleGraphObject::ResetCallbacks();

  auto first = LifecycleGraphObject::ptr::Create(CreateWith{domain}.with_id(1));
  auto second =
      LifecycleGraphObject::ptr::Create(CreateWith{domain}.with_id(2));
  first->first = second;

  auto graph = DomainGraph{&domain};
  Ptr<Obj> loaded_first = first.Load();
  auto result = graph.SaveRoot(loaded_first, ObjId{1});
  TEST_ASSERT_FALSE(result.IsOk());
  TEST_ASSERT_EQUAL(kNestedWriteError.error_code, result.error().error_code);
  TEST_ASSERT_EQUAL_PTR(kNestedWriteError.message.data(),
                        result.error().message.data());
  TEST_ASSERT_EQUAL(0, LifecycleGraphObject::saved_count);
}

// Verifies nested load failures propagate and suppress all load notifications.
void test_NestedLoadFailurePropagatesAndDoesNotNotify() {
  auto domain_storage = MapDomainStorage{};
  Domain save_domain{domain_storage};
  auto first =
      LifecycleGraphObject::ptr::Create(CreateWith{save_domain}.with_id(1));
  auto second =
      LifecycleGraphObject::ptr::Create(CreateWith{save_domain}.with_id(2));
  first->first = second;
  Ptr<Obj> loaded_first = first.Load();
  TEST_ASSERT_TRUE(
      DomainGraph{&save_domain}.SaveRoot(loaded_first, ObjId{1}).IsOk());
  domain_storage.Remove(ObjId{2});

  LifecycleGraphObject::ResetCallbacks();
  Domain load_domain{domain_storage};
  Ptr<Obj> loaded;
  auto graph = DomainGraph{&load_domain};
  TEST_ASSERT_FALSE(graph.LoadRoot(ObjId{1}, loaded).IsOk());
  TEST_ASSERT_NULL(loaded.get());
  TEST_ASSERT_EQUAL(0, LifecycleGraphObject::loaded_count);
}

// Verifies noexcept and overloaded lifecycle callbacks are discovered and
// invoked.
void test_NoexceptAndOverloadedCallbacksAreNotified() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  LifecycleNoexceptObject::ResetCallbacks();

  auto object =
      LifecycleNoexceptObject::ptr::Create(CreateWith{domain}.with_id(1));
  object->value = 7;
  object.Save();
  TEST_ASSERT_EQUAL(1, LifecycleNoexceptObject::saved_count);

  LifecycleNoexceptObject::ResetCallbacks();
  Domain load_domain{domain_storage};
  auto loaded =
      LifecycleNoexceptObject::ptr::Declare(CreateWith{load_domain}.with_id(1));
  loaded.Load();
  TEST_ASSERT_EQUAL(1, LifecycleNoexceptObject::loaded_count);
}
}  // namespace
}  // namespace ae::test_lifecycle

int test_lifecycle() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_lifecycle::test_SuccessfulCallbacksAreSynchronous);
  RUN_TEST(ae::test_lifecycle::test_FailedOperationsDoNotNotify);
  RUN_TEST(ae::test_lifecycle::test_AbsentOrRemovedDataDoesNotNotify);
  RUN_TEST(
      ae::test_lifecycle::test_DerivedCallbacksRequireExplicitBaseChaining);
  RUN_TEST(ae::test_lifecycle::test_InheritedCallbacksAreNotified);
  RUN_TEST(ae::test_lifecycle::
               test_CyclicSharedSerializationReportsSuccessAndNotifiesOnce);
  RUN_TEST(
      ae::test_lifecycle::test_NestedSaveFailurePropagatesAndDoesNotNotify);
  RUN_TEST(
      ae::test_lifecycle::test_NestedLoadFailurePropagatesAndDoesNotNotify);
  RUN_TEST(ae::test_lifecycle::test_NoexceptAndOverloadedCallbacksAreNotified);
  return UNITY_END();
}
