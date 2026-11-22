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

#include "aether-objects/obj/obj_ptr_base.h"

#include <utility>

#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

namespace ae {

ObjectPtrBase::ObjectPtrBase()
    : domain_{nullptr}, flags_{ObjFlags::kUnloaded} {}

ObjectPtrBase::ObjectPtrBase(Domain* domain, ObjId obj_id, ObjFlags flags)
    : domain_{domain}, id_{obj_id}, flags_{flags} {}

ObjectPtrBase::ObjectPtrBase(Domain* domain, ObjId obj_id, ObjFlags flags,
                             Ptr<Obj> ptr) noexcept
    : domain_{domain}, id_{obj_id}, flags_{flags}, cached_{std::move(ptr)} {}

ObjectPtrBase::ObjectPtrBase(ObjectPtrBase const& ptr) noexcept
    : domain_{ptr.domain_}, id_{ptr.id_}, flags_{ptr.flags_} {
  if (ptr.cached_) {
    cached_ = ptr.cached_;
  }
}

ObjectPtrBase::ObjectPtrBase(ObjectPtrBase&& ptr) noexcept
    : domain_{ptr.domain_}, id_{ptr.id_}, flags_{ptr.flags_} {
  if (ptr.cached_) {
    cached_ = std::move(ptr.cached_);
  }
}

ObjectPtrBase::~ObjectPtrBase() = default;

ObjId ObjectPtrBase::id() const {
  if (cached_) {
    return cached_->obj_id;
  }
  return id_;
}
ObjFlags ObjectPtrBase::flags() const { return flags_; }
Domain* ObjectPtrBase::domain() const {
  if (cached_) {
    return cached_->domain;
  }
  return domain_;
}

void ObjectPtrBase::SetFlags(ObjFlags flags) { flags_ = flags; }

bool ObjectPtrBase::is_valid() const { return id_.is_valid(); }

bool ObjectPtrBase::is_loaded() const { return static_cast<bool>(cached_); }

Ptr<Obj> const& ObjectPtrBase::LoadCached() {
  if (cached_) {
    return cached_;
  }
  if (!is_valid()) {
    return cached_;
  }
  cached_ = DomainGraph{domain_}.LoadRoot(id_);
  if (cached_) {
    flags_ = flags_ & ~ObjFlags::kUnloaded;
    id_ = cached_->obj_id;
    domain_ = cached_->domain;
  }
  return cached_;
}

Ptr<Obj> const& ObjectPtrBase::LoadCached() const {
  return const_cast<ObjectPtrBase*>(this)->LoadCached();  // NOLINT(*const-cast)
}

void ObjectPtrBase::Save() const {
  if (cached_) {
    DomainGraph{domain_}.SaveRoot(cached_, id_);
  }
}

void ObjectPtrBase::Reset() {
  if (cached_) {
    id_ = cached_->obj_id;
    domain_ = cached_->domain;
  }
  cached_.Reset();
  flags_ = flags_ | ObjFlags::kUnloaded;
}

Ptr<Obj> const& ObjectPtrBase::cached() const { return cached_; }
Ptr<Obj>& ObjectPtrBase::cached() { return cached_; }

ObjectPtrBase& ObjectPtrBase::operator=(ObjectPtrBase const& ptr) noexcept {
  if (this != &ptr) {
    domain_ = ptr.domain_;
    id_ = ptr.id_;
    flags_ = ptr.flags_;
    if (ptr.cached_) {
      cached_ = ptr.cached_;
    } else {
      cached_.Reset();
    }
  }
  return *this;
}

ObjectPtrBase& ObjectPtrBase::operator=(ObjectPtrBase&& ptr) noexcept {
  if (this != &ptr) {
    domain_ = ptr.domain_;
    id_ = ptr.id_;
    flags_ = ptr.flags_;

    // Ptr move assignment swaps storage after resetting its destination. Move
    // through a temporary and clear this cache first so ptr cannot receive the
    // cache previously held by this ObjectPtrBase.
    auto cache = std::move(ptr.cached_);
    cached_ = Ptr<Obj>{};
    cached_ = std::move(cache);
  }
  return *this;
}

namespace seri {
using ObjPtrBaseSerializer =
    Serializer<BinaryArchive<DomainBuffer>, ObjectPtrBase>;

SeriResult ObjPtrBaseSerializer::Seri(Archive& archive,
                                      Meta<ObjectPtrBase const> meta) {
  // Mutation is opt-in to a scoped operation on an owned mutable graph. The
  // ordinary persistence/publication walk leaves this callback unset.
  archive.buffer().domain_graph->NotifyObjectReference(
      const_cast<ObjectPtrBase&>(meta.value));
  if (meta.value.id_.is_valid()) {
    archive.buffer().domain_graph->NotifyCloneReference(meta.value.id_);
  }
  ObjId const serialized_id =
      archive.buffer().domain_graph->CloneSaveRemapId(meta.value.id_);
  TRY_RESULT((archive.buffer().Write(DataTag{serialized_id})));
  ObjFlags::Type publish_flag_bits =
      static_cast<ObjFlags::Type>(meta.value.flags_);
  if (archive.buffer().domain_graph->serialization_scope ==
          GraphSerializationScope::MaterializedPublication &&
      meta.value.cached_) {
    ObjFlags::Type const lazy_flags =
        static_cast<ObjFlags::Type>(ObjFlags::kUnloadedByDefault) |
        static_cast<ObjFlags::Type>(ObjFlags::kUnloaded);
    publish_flag_bits = static_cast<ObjFlags::Type>(
        publish_flag_bits & static_cast<ObjFlags::Type>(~lazy_flags));
  }
  ObjFlags const publish_flags{publish_flag_bits};
  TRY_RESULT((archive.buffer().Write(DataTag{publish_flags})));
  if (meta.value.cached_ &&
      archive.buffer().domain_graph->CloneSaveShouldTraverse(meta.value.id_)) {
    TRY_RESULT(archive.buffer().domain_graph->SaveRoot(meta.value.cached_,
                                                       meta.value.id_));
  }
  return Ok{seri::good};
}

SeriResult ObjPtrBaseSerializer::Deseri(Archive& archive,
                                        Meta<ObjectPtrBase> meta) {
  meta.value.domain_ = archive.buffer().domain_graph->domain;
  TRY_RESULT((archive.buffer().Read(DataTag{meta.value.id_})));
  TRY_RESULT((archive.buffer().Read(DataTag{meta.value.flags_})));
  if (meta.value.is_valid() &&
      (meta.value.flags_ & ObjFlags::kUnloadedByDefault) == 0 &&
      (meta.value.flags_ & ObjFlags::kUnloaded) == 0) {
    // cached_ keeps a live instance with this id alive through the lookup, so
    // a re-read reference keeps identity instead of rebuilding the object.
    TRY_RESULT(archive.buffer().domain_graph->LoadRoot(meta.value.id_,
                                                       meta.value.cached_));
  } else if (meta.value.is_valid() &&
             archive.buffer().domain_graph->retain_loaded_references) {
    // Preserve already constructed snapshot children without loading absent
    // lazy references or changing their serialized flags.
    meta.value.cached_ = meta.value.domain_->Find(meta.value.id_);
  } else {
    meta.value.cached_.Reset();
  }
  return Ok{seri::good};
}
}  // namespace seri
}  // namespace ae
