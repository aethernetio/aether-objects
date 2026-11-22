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

#include "aether-objects/obj/obj.h"

#include "aether-miscpp/crc.h"

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/registry.h"

namespace ae {

Ptr<Obj> ObjLayerFactoryCreateMustNotRun() { return {}; }

std::optional<seri::SeriResult> LoadObjLayer(DomainGraph* domain_graph,
                                             Ptr<Obj>& obj, ObjId storage_id) {
  return domain_graph->Load(static_cast<Obj&>(*obj.get()), storage_id);
}

std::optional<seri::SeriResult> SaveObjLayerUnused(DomainGraph*,
                                                   Ptr<Obj> const&, ObjId) {
  return Ok{seri::good};
}

void RegisterObjLayerFactory(Registry& registry) {
  if (registry.IsExisting(Obj::kClassId)) {
    return;
  }
  registry.RegisterClass(
      Obj::kClassId, Obj::kClassId,
      Factory{ObjLayerFactoryCreateMustNotRun, LoadObjLayer,
              SaveObjLayerUnused,
#ifndef NDEBUG
              std::string{"ae::Obj"},
              Obj::kClassId,
              Obj::kClassId,
#endif
      });
}

Obj::Obj() = default;

Obj::Obj(ObjProp prop) : domain{prop.domain}, obj_id{prop.id} {
  SetObjectScope(prop.domain->object_scope());
}

Obj::~Obj() {
  if (registered_objects_) {
    registered_objects_->Remove(obj_id.id());
  }
}

uint32_t Obj::GetClassId() const { return kClassId; }

void Obj::SetOriginId(ObjId origin_id) {
  if (!origin_id.is_valid() || domain == nullptr) {
    origin_.Reset();
    return;
  }
  origin_ = Obj::ptr::Declare(
      CreateWith{domain}.with_id(origin_id).with_flags(
          ObjFlags::kUnloadedByDefault | ObjFlags::kUnloaded));
}

ObjId Obj::origin_id() const { return origin_.id(); }

namespace domain_visitor {
std::size_t GetObjIndexImpl(Obj const* obj, std::uint32_t class_id) {
  auto res = crc32::from_buffer(
      reinterpret_cast<std::uint8_t const*>(&class_id), sizeof(class_id));
  auto id = obj->obj_id.id();
  res = crc32::from_buffer(reinterpret_cast<std::uint8_t const*>(&id),
                           sizeof(id), res);
  return res.value;
}
}  // namespace domain_visitor
}  // namespace ae
