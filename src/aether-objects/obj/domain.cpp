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

#include <algorithm>
#include <cstdlib>

#include "aether-objects/obj/domain.h"

#include "aether-objects/log.h"

namespace ae {

DomainGraph::DomainGraph(Domain* domain) : DomainGraph(domain, GraphSerializationScope::LocalPersistent) {}

DomainGraph::DomainGraph(Domain* domain, GraphSerializationScope scope)
    : domain(domain), serialization_scope(scope) {
  assert(domain);
}

Ptr<Obj> DomainGraph::LoadRoot(ObjId obj_id) {
  Ptr<Obj> ptr;
  if (LoadRoot(obj_id, ptr).IsErr()) {
    return {};
  }
  return ptr;
}

seri::SeriResult DomainGraph::LoadRoot(ObjId obj_id, Ptr<Obj>& ptr) {
  if (!obj_id.is_valid()) {
    return Error{seri::read_error};
  }
  if (ptr = domain->FindLocal(obj_id); ptr) {
    return Ok{seri::good};
  }
  // A cold local object shadows an already-loaded ancestor with the same ID.
  // Use the explicit snapshot namespace when hydration redirects layer reads.
  auto& lookup_storage = load_storage_ != nullptr
                             ? *load_storage_ : domain->domain_storage();
  if (lookup_storage.Enumerate(obj_id).empty()) {
    if (ptr = domain->Find(obj_id); ptr) {
      return Ok{seri::good};
    }
  }

  auto* factory = domain->GetMostRelatedFactory(obj_id);
  if (factory == nullptr) {
    return Error{seri::read_error};
  }

  // Load marks belong to an instance: a previous instance with this id was
  // released, so its marks must not skip layers of the new one.
  cycle_detector.Remove(obj_id);
  ptr = domain->ConstructObj(*factory, obj_id);
  LoadStoredLayers(ptr, obj_id);
  if (!ptr) {
    return Error{seri::read_error};
  }
  return Ok{seri::good};
}

Ptr<Obj> DomainGraph::LoadCopyImpl(ObjId ref_id, ObjId copy_id) {
  if (!ref_id.is_valid() || !copy_id.is_valid()) {
    return {};
  }
  // if already loaded
  if (auto obj = domain->Find(copy_id); obj) {
    return obj;
  }

  auto* factory = domain->GetMostRelatedFactory(ref_id);
  if (factory == nullptr) {
    assert(false);
    return {};
  }

  auto ptr = domain->ConstructObj(*factory, copy_id);
  LoadStoredLayers(ptr, ref_id);
  if (!ptr) {
    return {};
  }
  return ptr;
}

void DomainGraph::LoadStoredLayers(Ptr<Obj>& obj, ObjId storage_id) {
  for (auto const class_id : domain->KnownStoredClasses(storage_id)) {
    auto* layer = domain->FindClassFactory(class_id);
    assert(layer && layer->load);
    auto result = layer->load(this, obj, storage_id);
    if (result && result->IsErr()) {
      domain->RemoveObject(obj.get());
      obj.Reset();
      return;
    }
  }
}

seri::SeriResult DomainGraph::SaveRoot(Ptr<Obj> const& ptr, ObjId obj_id) {
  if (!ptr) {
    return Error{seri::write_error};
  }
  if (clone_save_remap_ != nullptr && !CloneSaveShouldTraverse(obj_id)) {
    return Ok{seri::good};
  }
  if (auto* factory = domain->FindClassFactory(ptr->GetClassId());
      factory != nullptr) {
    auto result = factory->save(this, ptr, obj_id);
    return result.value_or(Ok{seri::good});
  }
  return Error{seri::write_error};
}

ObjId DomainGraph::CloneSaveRemapId(ObjId id) const {
  if (clone_save_remap_ == nullptr || !id.is_valid()) {
    return id;
  }
  if (auto it = clone_save_remap_->find(id.id()); it != clone_save_remap_->end()) {
    return ObjId{it->second};
  }
  return id;
}

bool DomainGraph::CloneSaveShouldTraverse(ObjId id) const {
  if (clone_save_remap_ == nullptr) {
    return true;
  }
  return clone_save_remap_->find(id.id()) != clone_save_remap_->end();
}

DomainLoad DomainGraph::GetReader(DomainQuery const& query) {
  if (load_storage_ != nullptr) {
    // An explicit snapshot owns the entire read view, including absent layers.
    return load_storage_->Load(query);
  }
  for (auto* current = domain; current != nullptr;
       current = current->parent_domain()) {
    auto load = current->domain_storage().Load(query);
    if (load.result != DomainLoadResult::kEmpty) {
      return load;
    }
  }
  return DomainLoad{DomainLoadResult::kEmpty, {}};
}

std::unique_ptr<IDomainStorageWriter> DomainGraph::GetWriter(
    DomainQuery const& query) {
  DomainQuery store_query = query;
  store_query.force_materialize =
      store_query.force_materialize || force_materialize;
  if (clone_save_remap_ != nullptr) {
    store_query.id = CloneSaveRemapId(store_query.id);
    if (store_query.object_scope) {
      store_query.object_scope->root_id = CloneSaveRemapId(store_query.object_scope->root_id);
      store_query.object_scope->parent_root_id = CloneSaveRemapId(store_query.object_scope->parent_root_id);
    }
  }
  auto writer = domain->storage_->Store(store_query);
  assert(writer && "Writer must be created!");
  return writer;
}

Domain::Domain(IDomainStorage& storage) : Domain(storage, nullptr) {}

Domain::Domain(IDomainStorage& storage, Env* env)
    : storage_{&storage},
      registry_{&Registry::GetRegistry()},
      env_{env},
      registered_objects_{std::make_shared<DomainRegisteredObjects>()} {}

void DomainRegisteredObjects::Add(ObjId id, Ptr<Obj> const& obj) {
  by_id[id.id()] = obj;
}

void DomainRegisteredObjects::Remove(ObjId::Type id) {
  by_id.erase(id);
}

Ptr<Obj> Domain::ConstructObj(Factory const& factory, ObjId obj_id) {
  Ptr<Obj> o = factory.create();
  AddObject(obj_id, o);
  return o;
}

bool Domain::IsLast(uint32_t class_id) const {
  return registry_->relations.find(class_id) == registry_->relations.end();
}

bool Domain::IsExisting(uint32_t class_id) const {
  return registry_->IsExisting(class_id);
}

Ptr<Obj> Domain::FindLocal(ObjId obj_id) const {
  if (!registered_objects_) {
    return {};
  }
  if (auto it = registered_objects_->by_id.find(obj_id.id());
      it != registered_objects_->by_id.end()) {
    return it->second.Lock();
  }
  return {};
}

Ptr<Obj> Domain::Find(ObjId obj_id) const {
  for (Domain const* current = this; current != nullptr;
       current = current->parent_domain_) {
    if (auto found = current->FindLocal(obj_id)) {
      return found;
    }
  }
  return {};
}

ObjId Domain::GenerateObjectId() {
  for (Domain* current = this; current != nullptr;
       current = current->parent_domain_) {
    auto* sequence = current->object_id_sequence_;
    if (sequence == nullptr) continue;
    if (sequence->next_ == 0 || sequence->next_ > sequence->last_) {
      std::abort();
    }
    return ObjId{static_cast<ObjId::Type>(sequence->next_++)};
  }
  return ObjId::GenerateUnique();
}

void Domain::AddObject(ObjId id, Ptr<Obj> const& obj) {
  if (obj) {
    if (!obj->has_object_scope()) obj->SetObjectScope(object_scope_);
    obj->registered_objects_ = registered_objects_;
    obj->domain = this;
    obj->obj_id = id;
  }
  registered_objects_->Add(id, obj);
}

void Domain::RemoveObject(Obj* ptr) {
  if (ptr == nullptr || !ptr->registered_objects_) {
    return;
  }
  ptr->registered_objects_->Remove(ptr->obj_id.id());
}

std::vector<std::uint32_t> Domain::KnownStoredClasses(ObjId id) {
  auto classes = storage_->Enumerate(id);
  // A local stored definition shadows ancestors even if none of its classes
  // are registered in this executable. Only an absent definition falls back.
  if (classes.empty()) {
    return parent_domain_ != nullptr ? parent_domain_->KnownStoredClasses(id)
                                     : std::vector<std::uint32_t>{};
  }
  classes.erase(
      std::remove_if(std::begin(classes), std::end(classes),
                     [this](auto const& c) { return !IsExisting(c); }),
      std::end(classes));
  std::sort(std::begin(classes), std::end(classes),
            [this](auto left, auto right) {
              if (left == right) {
                return false;
              }
              if (registry_->GenerationDistance(right, left) > 0) {
                return false;
              }
              if (registry_->GenerationDistance(left, right) > 0) {
                return true;
              }
              return left < right;
            });
  return classes;
}

Factory* Domain::GetMostRelatedFactory(ObjId id) {
  auto const enumerated = storage_->Enumerate(id);
  if (enumerated.empty()) {
    return parent_domain_ != nullptr ? parent_domain_->GetMostRelatedFactory(id)
                                     : nullptr;
  }
  auto classes = KnownStoredClasses(id);
  if (classes.empty()) {
    return nullptr;
  }

#ifndef NDEBUG
  auto class_names = std::vector<std::string_view>{};
  class_names.reserve(enumerated.size());
  for (auto cid : enumerated) {
    class_names.emplace_back(registry_->ClassName(cid));
  }

  AE_LOG_MACRO_TAGGED(::ae::objects_tele::kDomainEnumeratedNames,
                      "For obj {} enumerated classes [{}]", id.id(),
                      class_names);
#else
  AE_LOG_MACRO_TAGGED(::ae::objects_tele::kDomainEnumeratedClasses,
                      "For obj {} enumerated classes [{}]", id.id(), enumerated);
#endif

  bool const had_unknown_stored_class = [&] {
    for (auto const class_id : enumerated) {
      if (!IsExisting(class_id) && class_id != Obj::kClassId) {
        return true;
      }
    }
    return false;
  }();

  if (had_unknown_stored_class) {
    return nullptr;
  }

  if (classes.back() == Obj::kClassId) {
    return nullptr;
  }

  return FindClassFactory(classes.back());
}

Factory* Domain::FindClassFactory(std::uint32_t class_id) {
  return registry_->FindFactory(class_id);
}

namespace seri {
SeriResult Serializer<BinaryArchive<DomainBuffer>, std::string>::Deseri(
    BinaryArchive<DomainBuffer>& archive, Meta<std::string> val) const {
  std::size_t size{};
  TRY_RESULT(archive.buffer().Read(SizeTag{size}));
  if (size > archive.max_container_load_size()) {
    return Error{container_too_large};
  }
  val.value.resize(size);
  auto* data = reinterpret_cast<std::uint8_t*>(val.value.data());
  return archive.buffer().Read(DataReadTag{data, size});
}

SeriResult Serializer<BinaryArchive<DomainBuffer>, std::string>::Seri(
    BinaryArchive<DomainBuffer>& archive, Meta<std::string const> val) const {
  auto const size = val.value.size();
  TRY_RESULT(archive.buffer().Write(SizeTag{size}));
  auto const* data = reinterpret_cast<std::uint8_t const*>(val.value.data());
  TRY_RESULT(archive.buffer().Write(DataWriteTag{data, size}));
  return Ok{good};
}

SeriResult
Serializer<BinaryArchive<DomainBuffer>, std::vector<std::uint8_t>>::Deseri(
    BinaryArchive<DomainBuffer>& archive,
    Meta<std::vector<std::uint8_t>> val) const {
  std::size_t size{};
  TRY_RESULT(archive.buffer().Read(SizeTag{size}));
  if (size > archive.max_container_load_size()) {
    return Error{container_too_large};
  }
  val.value.resize(size);
  auto* data = reinterpret_cast<std::uint8_t*>(val.value.data());
  return archive.buffer().Read(DataReadTag{data, size});
}

SeriResult
Serializer<BinaryArchive<DomainBuffer>, std::vector<std::uint8_t>>::Seri(
    BinaryArchive<DomainBuffer>& archive,
    Meta<std::vector<std::uint8_t> const> val) const {
  auto const size = val.value.size();
  TRY_RESULT(archive.buffer().Write(SizeTag{size}));
  auto const* data = reinterpret_cast<std::uint8_t const*>(val.value.data());
  TRY_RESULT(archive.buffer().Write(DataWriteTag{data, size}));
  return Ok{good};
}
}  // namespace seri
}  // namespace ae
