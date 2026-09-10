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

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "aether-objects/env/env.h"
#include "aether-objects/obj/domain.h"
#include "map_domain_storage.h"

namespace ae::test_env {

/// \brief Component whose default type-derived ID is used for lookup tests.
struct DefaultComponent {
  int value{};
};

/// \brief Component whose explicit ID specialization is used for lookup tests.
struct CustomComponent {
  int value{};
};

/// \brief Explicit environment ID assigned to CustomComponent.
inline constexpr EnvId kCustomComponentId = 0x12345678U;

}  // namespace ae::test_env

namespace ae {
template <>
struct EnvTypeId<test_env::CustomComponent> {
  static constexpr EnvId value = test_env::kCustomComponentId;
};

}  // namespace ae

namespace ae::test_env {

/// \brief Environment fixture that exposes default and custom-ID components.
class TestEnv final : public Env {
 public:
  DefaultComponent default_component{.value = 1};
  CustomComponent custom_component{.value = 2};

 protected:
  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<DefaultComponent>::value) {
      return &default_component;
    }
    if (id == EnvTypeId<CustomComponent>::value) {
      return &custom_component;
    }
    return nullptr;
  }
};

/// \brief Environment fixture that verifies private components remain usable.
class PrivateComponentEnv final : public Env {
 public:
  /// \brief Verifies this fixture can look up its private component type.
  void VerifyAccess() {
    TEST_ASSERT_EQUAL(3, get<PrivateComponent>().value);
    auto& env = static_cast<Env&>(*this);
    TEST_ASSERT_EQUAL_PTR(&component_, env.try_get<PrivateComponent>());
  }

 private:
  struct PrivateComponent {
    int value{3};
  };

 protected:
  void* find_component(EnvId id) noexcept override {
    return id == EnvTypeId<PrivateComponent>::value ? &component_ : nullptr;
  }

 private:
  PrivateComponent component_;
};

/// \brief Provider fixture that returns a nullable environment and counts
/// calls.
struct Provider {
  Env* env{};
  mutable int calls{};

  Env* get_env() const {
    ++calls;
    return env;
  }
};

/// \brief Invalid provider fixture that returns a const environment pointer.
struct ConstEnvProvider {
  Env const* get_env() const;
};

/// \brief Invalid provider fixture that returns an environment reference.
struct ReferenceEnvProvider {
  Env& get_env() const;
};

/// \brief Type convertible to an environment pointer but not an environment
/// pointer.
struct EnvPointerConvertible {
  explicit operator Env*() const;
};

/// \brief Invalid provider fixture that returns a pointer-convertible value.
struct ConvertibleEnvProvider {
  EnvPointerConvertible get_env() const;
};

template <typename T>
concept HasTryGet = requires(T const& provider) { provider.try_get(); };

template <typename T>
concept HasEnvTryGet = requires(Env& env) { env.template try_get<T>(); };

static_assert(sizeof(EnvId) == 4);
static_assert(std::same_as<EnvId, std::uint32_t>);
static_assert(EnvTypeId<CustomComponent>::value == kCustomComponentId);
static_assert(EnvProvider<Provider>);
static_assert(!EnvProvider<ConstEnvProvider>);
static_assert(!EnvProvider<ReferenceEnvProvider>);
static_assert(!EnvProvider<ConvertibleEnvProvider>);
static_assert(EnvProvider<Domain>);
static_assert(!HasTryGet<Domain>);
static_assert(!std::is_invocable_v<
              decltype(&Env::template get<DefaultComponent>), Env const&>);
static_assert(!std::is_invocable_v<
              decltype(&Env::template try_get<DefaultComponent>), Env const&>);
static_assert(HasEnvTryGet<DefaultComponent>);
static_assert(std::same_as<
              decltype(std::declval<Env&>().template get<DefaultComponent>()),
              DefaultComponent&>);

/// \brief Verifies typed lookup finds registered components and misses unknown
/// IDs.
void test_EnvComponentLookup() {
  TestEnv env;
  TEST_ASSERT_EQUAL(1, env.get<DefaultComponent>().value);
  TEST_ASSERT_EQUAL_PTR(&env.default_component,
                        env.try_get<DefaultComponent>());
  TEST_ASSERT_EQUAL(1, env.get<DefaultComponent>().value);
  TEST_ASSERT_EQUAL(2, env.get<CustomComponent>().value);
  TEST_ASSERT_NULL(env.try_get<int>());

  PrivateComponentEnv private_env;
  private_env.VerifyAccess();
}

/// \brief Verifies providers expose environments for direct component lookup.
void test_EnvHelpers() {
  TestEnv env;
  Provider const provider{.env = &env};
  TEST_ASSERT_EQUAL(1, get_env<DefaultComponent>(provider).value);
  TEST_ASSERT_EQUAL(1, provider.calls);
  TEST_ASSERT_EQUAL(1, get_env<DefaultComponent>(provider).value);
  TEST_ASSERT_EQUAL(2, provider.calls);
  TEST_ASSERT_EQUAL_PTR(&env.custom_component,
                        try_get_env<CustomComponent>(provider));
  TEST_ASSERT_EQUAL(3, provider.calls);
  TEST_ASSERT_EQUAL_PTR(&env.custom_component,
                        provider.get_env()->try_get<CustomComponent>());
  TEST_ASSERT_EQUAL(4, provider.calls);

  Provider const null_provider{};
  TEST_ASSERT_NULL(null_provider.get_env());
  TEST_ASSERT_EQUAL(1, null_provider.calls);
}

/// \brief Verifies Domain returns its optional environment through get_env and
/// supports EnvProvider component lookup.
void test_DomainEnv() {
  MapDomainStorage storage;
  Domain const without_env{storage};
  TEST_ASSERT_NULL(without_env.get_env());

  TestEnv env;
  Domain const with_env{storage, &env};
  TEST_ASSERT_EQUAL_PTR(&env, with_env.get_env());
  TEST_ASSERT_EQUAL_PTR(&env.default_component,
                        with_env.get_env()->try_get<DefaultComponent>());
}

}  // namespace ae::test_env

/// \brief Runs the environment component and provider contract test suite.
int run_test_env() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_env::test_EnvComponentLookup);
  RUN_TEST(ae::test_env::test_EnvHelpers);
  RUN_TEST(ae::test_env::test_DomainEnv);
  return UNITY_END();
}
