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

// Copyright 2016 Aether authors. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//   http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#ifndef AETHER_OBJECTS_OBJ_OBJ_H_
#define AETHER_OBJECTS_OBJ_OBJ_H_

#include <cstdint>
#include <type_traits>

#include "aether-miscpp/crc.h"
#include "aether-miscpp/reflect/reflect.h"

#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr.h"
#include "aether-objects/obj/registrar.h"  // IWYU pragma:  export
#include "aether-objects/obj/registry.h"

namespace ae {

class Domain;

/**
 * \brief Base class for all objects.
 */
class Obj {
  friend class ObjectPtrBase;

 public:
  using CurrentVersion = Version<0>;
  using ptr = ObjPtr<Obj>;

  static constexpr std::uint32_t kClassId = crc32::from_literal("Obj").value;
  static constexpr std::uint32_t kBaseClassId =
      crc32::from_literal("Obj").value;

  static constexpr CurrentVersion kCurrentVersion{};

  Obj();
  explicit Obj(ObjProp prop);
  virtual ~Obj();

  virtual std::uint32_t GetClassId() const;

  void SetOriginId(ObjId origin_id);
  ObjId origin_id() const;

  ObjectScope object_scope() const { return object_scope_.value_or(ObjectScope{}); }
  bool has_object_scope() const { return object_scope_.has_value(); }
  void SetObjectScope(ObjectScope scope) { object_scope_ = scope; }

  // Obj/S0: the lazy origin reference is part of the base object schema.
  AE_REFLECT(AE_MMBR(origin_));

  Domain* domain{};
  ObjId obj_id;
  std::shared_ptr<DomainRegisteredObjects> registered_objects_;

 private:
  ObjPtr<Obj> origin_{};
  std::optional<ObjectScope> object_scope_{};
};

#include "aether-objects/obj/domain.h"

template <typename T>
template <typename... TArgs>
ObjPtr<T> ObjPtr<T>::Create(CreateWith create_arg, TArgs&&... args) {
  static_assert(obj_ptr_internal::IsObjType<T>::value,
                "T Must be an object type");
  auto* domain = create_arg.domain;
  auto obj_id = create_arg.obj_id.has_value()
                    ? *create_arg.obj_id : domain->GenerateObjectId();
  auto flags = create_arg.flags.value_or(ObjFlags{});
  static_assert(
      std::is_constructible_v<T, ObjProp, TArgs...>,
      "Class must be constructible with passed arguments and Domain*");
  auto ptr = MakePtr<T>(ObjProp{domain, obj_id}, std::forward<TArgs>(args)...);
  domain->AddObject(obj_id, ptr);

  return ObjPtr<T>(domain, obj_id, flags, std::move(ptr));
}

template <typename T>
ObjPtr<T> ObjPtr<T>::Declare(CreateWith create_arg) {
  static_assert(obj_ptr_internal::IsObjType<T>::value,
                "T Must be an object type");
  auto* domain = create_arg.domain;
  auto obj_id = create_arg.obj_id.has_value()
                    ? *create_arg.obj_id : domain->GenerateObjectId();
  auto flags = create_arg.flags.value_or(ObjFlags::kUnloaded);
  return ObjPtr<T>(domain, obj_id, flags);
}

template <typename T>
ObjPtr<T> ObjPtr<T>::Clone(ObjId obj_id) const {
  assert(is_valid() && "Invalid clone ptr");
  assert(obj_id.is_valid() && "Invalid object ID");
  auto ptr = DomainGraph{domain()}.template LoadCopy<T>(id(), obj_id);
  return ObjPtr<T>{domain(), obj_id,
                   static_cast<std::uint8_t>(flags() & ~ObjFlags::kUnloaded),
                   std::move(ptr)};
}

namespace domain_visitor {
extern std::size_t GetObjIndexImpl(Obj const* obj, std::uint32_t class_id);

template <typename T>
struct ObjectIndex<T, std::enable_if_t<std::is_base_of_v<Obj, T>>> {
  static std::size_t GetIndex(Obj const* obj) {
    return GetObjIndexImpl(obj, T::kClassId);
  }
};
}  // namespace domain_visitor
}  // namespace ae

#define _AE_OBJECT_FIELDS(CLASS_ID, BASE_CLASS_ID, VERSION)    \
  static constexpr std::uint32_t kClassId = CLASS_ID;          \
  static constexpr std::uint32_t kBaseClassId = BASE_CLASS_ID; \
  static constexpr std::uint32_t kVersion = VERSION;           \
  using CurrentVersion = ::ae::Version<kVersion>;              \
  static constexpr CurrentVersion kCurrentVersion{};

/**
 * \brief Use it inside each derived class to register it with the object system
 */
#define AE_OBJECT(DERIVED, BASE, VERSION)                                \
 protected:                                                              \
  friend class ::ae::Registrar<DERIVED>;                                 \
  friend ::ae::Ptr<DERIVED> ae::MakePtr<DERIVED>();                      \
                                                                         \
 public:                                                                 \
  _AE_OBJECT_FIELDS(crc32::from_literal(#DERIVED).value, BASE::kClassId, \
                    VERSION)                                             \
  inline static auto registrar_ =                                        \
      ::ae::Registrar<DERIVED>(kClassId, kBaseClassId);                  \
                                                                         \
  using Base = BASE;                                                     \
  using ptr = ::ae::ObjPtr<DERIVED>;                                     \
                                                                         \
  Base& base_{*this};                                                    \
                                                                         \
  std::uint32_t GetClassId() const override { return kClassId; }         \
                                                                         \
 private:                                                                \
  /* add rest class's staff after */

/**
 * \brief Obj class reflection
 */
#define AE_OBJECT_REFLECT(...)                                         \
  AE_REFLECT(AE_REF_BASE(std::decay_t<decltype(base_)>) __VA_OPT__(, ) \
                 __VA_ARGS__)

#endif  // AETHER_OBJECTS_OBJ_OBJ_H_
