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

#ifndef AETHER_OBJECTS_ENV_ENV_H_
#define AETHER_OBJECTS_ENV_ENV_H_

#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include "aether-miscpp/meta/type_index.h"

namespace ae {

using EnvId = std::uint32_t;

static_assert(sizeof(EnvId) == 4);

/**
 * @brief Customizes the identifier used to find a component in an Env.
 *
 * Specialize this trait for an unqualified component type when a stable custom
 * identifier is required. The default is a CRC32 of GetTypeIndex<C>(). Its
 * value depends on compiler-generated type names, so it is not portable across
 * compilers and CRC32 collisions are possible. Env IDs are process-local
 * lookup keys only and must not be persisted. Specializations must be ODR
 * consistent throughout a program.
 */
template <typename T>
  requires(!std::is_const_v<T> && !std::is_volatile_v<T> &&
           !std::is_reference_v<T>)
struct EnvTypeId {
  static constexpr EnvId value = static_cast<EnvId>(GetTypeIndex<T>());
};

/**
 * \brief Provides lookup access to components in a concrete environment.
 */
class Env {
 public:
  virtual ~Env() = default;

  /**
   * \brief Returns the component of type `T`.
   *
   * The component must be available in this environment.
   */
  template <typename T>
  T& get() {
    auto* component = try_get<T>();
    assert(component != nullptr && "Env component is not available");
    if (component == nullptr) {
      std::abort();
    }
    return *component;
  }

  /**
   * \brief Returns the component of type `T`, or `nullptr` when unavailable.
   */
  template <typename T>
    requires(!std::is_const_v<T> && !std::is_volatile_v<T> &&
             !std::is_reference_v<T>)
  auto try_get() noexcept -> T* {
    return static_cast<T*>(find_component(EnvTypeId<T>::value));
  }

 protected:
  /**
   * \brief Finds the live component identified by @p id.
   *
   * Each concrete environment must support components with unique
   * EnvTypeId values. The default values are CRC32 hashes of compiler-generated
   * type names and can collide; custom IDs can collide as well. CRC32-based
   * default IDs also are not portable across compilers. A collision makes
   * component lookup ambiguous, so a concrete environment must choose unique
   * IDs for all of its supported components.
   *
   * Implementations should compare @p id with EnvTypeId<T>::value and return
   * the address of the corresponding live component, or nullptr for an
   * unknown ID. For a lookup of `T`, the returned pointer must point to an
   * object whose exact normalized type is `T` (without cv/ref qualifiers),
   * rather than a base or related type. The object must remain alive and at a
   * stable address for the entire time callers use the returned pointer.
   */
  virtual void* find_component(EnvId id) noexcept = 0;
};

/**
 * \brief Requires a provider that exposes an Env through get_env().
 */
template <typename P>
concept EnvProvider = requires(P const& provider) {
  { provider.get_env() } -> std::same_as<Env*>;
};

/**
 * \brief Returns the component of type `T` from @p provider's environment.
 *
 * The provider's get_env() must return an environment, and that environment
 * must provide the component. A null environment violates this contract and
 * aborts the program.
 */
template <typename T>
auto get_env(EnvProvider auto const& provider) -> T& {
  auto* env = provider.get_env();
  assert(env != nullptr && "Env provider does not have an environment");
  if (env == nullptr) {
    std::abort();
  }
  return env->template get<T>();
}

/**
 * \brief Returns the component of type `T` from @p provider's environment.
 *
 * Returns `nullptr` when the provider's get_env() returns `nullptr` or its
 * environment does not provide the component.
 */
template <typename T>
auto try_get_env(EnvProvider auto const& provider) -> T*
{
  auto* env = provider.get_env();
  return env == nullptr ? nullptr : env->template try_get<T>();
}

}  // namespace ae

#endif  // AETHER_OBJECTS_ENV_ENV_H_
