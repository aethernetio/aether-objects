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

#include "aether-objects/obj/domain.h"
#include "map_domain_storage.h"
#include "objects/seven_fridays.h"

namespace ae::test_update_objects {
constexpr float kStoredValue = 42.42F;

// replace class id in map domain storage to imitate one class save and load
void replace_class_id(MapDomainStorage& domain_storage, std::uint32_t old_class,
                      std::uint32_t new_class) {
  for (auto& [_, classes] : domain_storage.map) {
    auto it = classes.find(old_class);
    if (it != classes.end()) {
      classes[new_class] = std::move(it->second);
      classes.erase(it);
    }
  }
}

// remove class id from map domain storage to imitate one class was removed from
// inheritance chain
void remove_class_id(MapDomainStorage& domain_storage, std::uint32_t class_id) {
  for (auto& [_, classes] : domain_storage.map) {
    classes.erase(class_id);
  }
}

// Verifies stored objects can be loaded and saved through successive upgrades.
void test_IncreaseVersion() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  // the oldest version
  {
    Friday0::ptr friday = Friday0::ptr::Create(CreateWith{domain}.with_id(1));
    friday.Save();
  }

  // load version 1 from version 0
  replace_class_id(domain_storage, Friday0::kClassId, Friday1::kClassId);
  {
    Friday1::ptr friday = Friday1::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& p) {
      TEST_ASSERT(p);
      TEST_ASSERT_EQUAL(22, p->a);
      TEST_ASSERT_EQUAL(23, p->b);
      p->a = 123;
      p->b = 431;
    });
    friday.Save();
  }

  // load version 2 from version 1
  replace_class_id(domain_storage, Friday1::kClassId, Friday2::kClassId);
  {
    Friday2::ptr friday = Friday2::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& p) {
      TEST_ASSERT(p);
      TEST_ASSERT_EQUAL_FLOAT(123.0F, p->a);
      p->a = kStoredValue;
    });
    friday.Save();
  }

  // load version 3 from version 2
  replace_class_id(domain_storage, Friday2::kClassId, Friday3::kClassId);
  {
    Friday3::ptr friday = Friday3::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& p) {
      TEST_ASSERT(p);
      TEST_ASSERT_EQUAL_FLOAT(kStoredValue, p->a);
      TEST_ASSERT_EQUAL_STRING("", p->x.c_str());
    });
  }
}

// Verifies stored objects can be loaded and saved through successive
// downgrades.
void test_DecreaseVersion() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  // the newest version
  {
    Friday3::ptr friday = Friday3::ptr::Create(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) {
      f->x = "hello";
      f->a = 123.123f;
    });
    friday.Save();
  }

  // load version 2 from version 3
  replace_class_id(domain_storage, Friday3::kClassId, Friday2::kClassId);
  // it's not allowed to remove class from inheritance chain and leave it in
  // registry
  remove_class_id(domain_storage, Hoopa::kClassId);
  {
    Friday2::ptr friday = Friday2::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) {
      TEST_ASSERT(f);
      TEST_ASSERT_EQUAL_FLOAT(123.123F, f->a);
    });
    friday.Save();
  }

  // load version 1 from version 2
  replace_class_id(domain_storage, Friday2::kClassId, Friday1::kClassId);
  {
    Friday1::ptr friday = Friday1::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) {
      TEST_ASSERT(f);
      TEST_ASSERT_EQUAL(123, f->a);
      TEST_ASSERT_EQUAL(0, f->b);
      f->b = 23;
    });
    friday.Save();
  }

  // load version 0 from version 1
  replace_class_id(domain_storage, Friday1::kClassId, Friday0::kClassId);
  {
    Friday0::ptr friday = Friday0::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) { TEST_ASSERT(f); });
  }
}

// Verifies a missing newest record falls back to an older supported version.
void test_MissingVersionFallsBackToSupportedVersion() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  auto saved = Friday2::ptr::Create(CreateWith{domain}.with_id(1));
  saved->a = kStoredValue;
  saved.Save();

  // Remove the newest version record while retaining the older compatible one.
  domain_storage.map[1][Friday2::kClassId].erase(0);
  auto graph = DomainGraph{&domain};
  auto result = graph.Load(*saved, ObjId{1});

  TEST_ASSERT_TRUE(result.has_value());
  TEST_ASSERT_TRUE(result->IsOk());
  TEST_ASSERT_EQUAL_FLOAT(kStoredValue, saved->a);
}

// Verifies loading reports missing_version when no serialized version exists.
void test_AllMissingVersionsReturnMissingVersionError() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  auto object = Friday2::ptr::Create(CreateWith{domain}.with_id(1));
  auto graph = DomainGraph{&domain};
  auto result = graph.Load(*object, ObjId{1});

  TEST_ASSERT_TRUE(result.has_value());
  TEST_ASSERT_FALSE(result->IsOk());
  TEST_ASSERT_EQUAL(domain_internal::missing_version.error_code,
                    result->error().error_code);
}

// Verifies a malformed present version reports its error without fallback.
void test_RealVersionErrorStopsIteration() {
  auto domain_storage = MapDomainStorage{};
  Domain domain{domain_storage};
  auto saved = Friday2::ptr::Create(CreateWith{domain}.with_id(1));
  saved->a = kStoredValue;
  saved.Save();

  // Removing a version is recoverable, but corrupting a present record is not.
  // Version 0 has no serialized fields, whereas version 2 stores `a`.
  // Clearing version 2 therefore produces read_eof instead of a fallback.
  auto& version_data = domain_storage.map[1][Friday2::kClassId][2];
  TEST_ASSERT_TRUE(version_data.has_value());
  version_data.value().clear();
  auto graph = DomainGraph{&domain};
  auto result = graph.Load(*saved, ObjId{1});

  TEST_ASSERT_TRUE(result.has_value());
  TEST_ASSERT_FALSE(result->IsOk());
  TEST_ASSERT_EQUAL(seri::read_eof.error_code, result->error().error_code);
}
}  // namespace ae::test_update_objects

int test_update_objects() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_update_objects::test_IncreaseVersion);
  RUN_TEST(ae::test_update_objects::test_DecreaseVersion);
  RUN_TEST(
      ae::test_update_objects::test_MissingVersionFallsBackToSupportedVersion);
  RUN_TEST(ae::test_update_objects::
               test_AllMissingVersionsReturnMissingVersionError);
  RUN_TEST(ae::test_update_objects::test_RealVersionErrorStopsIteration);
  return UNITY_END();
}
