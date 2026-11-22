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

#ifndef AETHER_OBJECTS_OBJ_DOMAIN_H_
#define AETHER_OBJECTS_OBJ_DOMAIN_H_

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <unordered_map>

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"

#include "aether-objects/ptr/ptr_view.h"

#include "aether-objects/env/env.h"
#include "aether-objects/obj/domain_buffer.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/registry.h"
#include "aether-objects/obj/version_iterator.h"

namespace ae {

namespace domain_internal {
inline constexpr seri::SeriError missing_version{
    .error_code = 1000, .message = "Object version is missing"};
}  // namespace domain_internal

class Obj;
class Domain;
class DomainGraph;
class ObjectPtrBase;

struct DomainRegisteredObjects {
  std::map<ObjId::Type, PtrView<Obj>> by_id;

  void Add(ObjId id, Ptr<Obj> const& obj);
  void Remove(ObjId::Type id);
};

template <typename T>
class Registrar;

struct DomainCycleDetector {
  struct Node {
    bool operator==(const Node& other) const {
      return id == other.id && class_id == other.class_id;
    }
    bool operator<(const Node& other) const {
      return id < other.id || (id == other.id && class_id < other.class_id);
    }

    ObjId id;
    std::uint32_t class_id;
  };

  bool Add(std::uint32_t class_id, ObjId obj_id) {
    auto [_, ok] = visited_nodes.insert(Node{obj_id, class_id});
    return ok;
  }

  void Remove(ObjId obj_id) {
    for (auto it = visited_nodes.begin(); it != visited_nodes.end();) {
      if (it->id == obj_id) {
        it = visited_nodes.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::set<Node> visited_nodes;
};

// Serialization intent for one DomainGraph Save/Load walk. Owned by the
// DomainGraph instance (no process-global / thread_local policy registry).
enum class GraphSerializationScope : std::uint8_t {
  LocalPersistent = 0,
  NetworkShared = 1,
  // UI structural/incremental publication: materialized fields only; Node
  // historical base/journal must not expand the Save walk.
  MaterializedPublication = 2,
  // Canonical Clone snapshot: current fields and local refs; omit Node
  // historical base/journal.
  CanonicalClone = 3,
  // Historical Node base snapshot: materialized fields with layer ownership;
  // AppTraverse omits Node base/journal like publication/clone walks.
  HistoricalBaseSnapshot = 4,
};

/**
 * \brief Operations on graph of objects in domain.
 * For load/save a new graph create a new DomainGraph.
 */
class DomainGraph {
 public:
  explicit DomainGraph(Domain* domain);
  DomainGraph(Domain* domain, GraphSerializationScope scope);

  // Load saved state of object.
  Ptr<Obj> LoadRoot(ObjId obj_id);
  seri::SeriResult LoadRoot(ObjId obj_id, Ptr<Obj>& ptr);
  // Save state of object.
  seri::SeriResult SaveRoot(Ptr<Obj> const& ptr, ObjId obj_id);
  // Load a copy of object
  template <typename T>
  Ptr<T> LoadCopy(ObjId ref_id, ObjId copy_id);

  Ptr<Obj> LoadCopyImpl(ObjId ref_id, ObjId copy_id);

  template <typename T>
  std::optional<seri::SeriResult> Load(T& obj, ObjId obj_id);
  template <typename T, auto V>
  seri::SeriResult LoadVersion(Version<V> version, T& obj, ObjId obj_id);

  template <typename T>
  std::optional<seri::SeriResult> Save(T const& obj, ObjId obj_id);
  template <typename T, auto V>
  seri::SeriResult SaveVersion(Version<V> version, T const& obj, ObjId obj_id);

  // Redirect serialized layer reads for this walk only. Identity lookup and
  // object construction still use domain; the caller preconstructs any object
  // absent from its storage. The supplied storage must outlive this graph.
  // nullptr restores the ordinary domain/parent storage lookup.
  void SetLoadStorage(IDomainStorage* storage) { load_storage_ = storage; }

  // Optional operation-local reference walk. Clone finalization uses this to
  // relocate serialized pointer fields, including fields absent from
  // reflection. The context must outlive this DomainGraph; normal Save has no
  // visitor.
  using ObjectReferenceVisitorFn = void (*)(void*, ObjectPtrBase&);
  void SetObjectReferenceVisitor(void* context, ObjectReferenceVisitorFn fn) {
    object_reference_context_ = context;
    object_reference_visitor_ = fn;
  }
  void NotifyObjectReference(ObjectPtrBase& reference) {
    if (object_reference_visitor_ != nullptr) {
      object_reference_visitor_(object_reference_context_, reference);
    }
  }

  using CloneSaveRemapTable = std::unordered_map<ObjId::Type, ObjId::Type>;

  // Per-Save clone context: remaps storage keys and serialized ObjPtr ids.
  // Walk/cycle detection continues to use pre-remap ObjIds.
  void SetCloneSaveRemap(CloneSaveRemapTable const* table) {
    clone_save_remap_ = table;
  }

  using CloneReferenceVisitorFn = void (*)(void* context, ObjId id);

  void SetCloneReferenceVisitor(void* context, CloneReferenceVisitorFn fn) {
    clone_reference_visitor_context_ = context;
    clone_reference_visitor_ = fn;
  }

  void NotifyCloneReference(ObjId id) const {
    if (clone_reference_visitor_ != nullptr && id.is_valid()) {
      clone_reference_visitor_(clone_reference_visitor_context_, id);
    }
  }

  ObjId CloneSaveRemapId(ObjId id) const;
  bool CloneSaveShouldTraverse(ObjId id) const;

  Domain* domain{};
  DomainCycleDetector cycle_detector{};
  GraphSerializationScope serialization_scope{
      GraphSerializationScope::LocalPersistent};
  bool force_materialize{false};
  // Keep preconstructed references during scoped hydration, including lazy
  // references. Missing lazy objects remain unloaded; their flags are
  // unchanged.
  bool retain_loaded_references{false};

 private:
  void LoadStoredLayers(Ptr<Obj>& obj, ObjId storage_id);

  DomainLoad GetReader(DomainQuery const& query);
  std::unique_ptr<IDomainStorageWriter> GetWriter(DomainQuery const& query);

  IDomainStorage* load_storage_{nullptr};
  void* object_reference_context_{nullptr};
  ObjectReferenceVisitorFn object_reference_visitor_{nullptr};
  CloneSaveRemapTable const* clone_save_remap_{nullptr};
  void* clone_reference_visitor_context_{nullptr};
  CloneReferenceVisitorFn clone_reference_visitor_{nullptr};
};

inline bool IsMissingVersion(seri::SeriResult const& result) {
  return result.IsErr() && result.error().error_code ==
                               domain_internal::missing_version.error_code;
}

class Domain {
  friend class DomainGraph;

 public:
  explicit Domain(IDomainStorage& storage);
  Domain(IDomainStorage& storage, Env* env);

  /// \brief Returns this domain's optional environment.
  Env* get_env() const { return env_; }

  // Search local registry only.
  Ptr<Obj> FindLocal(ObjId obj_id) const;
  // Search local registry, then parent chain (not siblings or children).
  Ptr<Obj> Find(ObjId obj_id) const;

  void set_parent_domain(Domain* parent) { parent_domain_ = parent; }
  Domain* parent_domain() const { return parent_domain_; }

  // Default runtime allocation is random. A scoped producer may allocate an
  // explicit identity range; descendant domains share that producer's cursor.
  class ObjectIdSequence;
  ObjId GenerateObjectId();

  void SetObjectScope(ObjId root_id) {
    object_scope_ = ObjectScope{
        root_id,
        parent_domain_ ? parent_domain_->object_scope().root_id : ObjId{},
        false};
  }
  ObjectScope object_scope() const { return object_scope_; }

  void AddObject(ObjId id, Ptr<Obj> const& obj);
  void RemoveObject(Obj* obj);

  IDomainStorage& domain_storage() { return *storage_; }
  IDomainStorage const& domain_storage() const { return *storage_; }

 private:
  Ptr<Obj> ConstructObj(Factory const& factory, ObjId id);

  bool IsLast(std::uint32_t class_id) const;
  bool IsExisting(std::uint32_t class_id) const;

  Factory* FindClassFactory(std::uint32_t class_id);
  Factory* GetMostRelatedFactory(ObjId id);
  std::vector<std::uint32_t> KnownStoredClasses(ObjId id);

  IDomainStorage* storage_;
  Registry* registry_;
  Env* env_{};
  Domain* parent_domain_{nullptr};
  ObjectScope object_scope_{};
  ObjectIdSequence* object_id_sequence_{nullptr};

  std::shared_ptr<DomainRegisteredObjects> registered_objects_;
};

// Producer-only allocation policy. Its lifetime cannot outlive its Domain.
// Nested policies restore the previous policy; separate domains never share
// allocator state unless explicitly related through parent_domain.
class Domain::ObjectIdSequence {
  friend class Domain;

 public:
  ObjectIdSequence(Domain& domain, ObjId first, ObjId last)
      : domain_{domain},
        previous_{domain.object_id_sequence_},
        next_{first.id()},
        last_{last.id()} {
    assert(first.is_valid() && first.id() <= last.id());
    domain_.object_id_sequence_ = this;
  }
  ~ObjectIdSequence() { domain_.object_id_sequence_ = previous_; }
  ObjectIdSequence(ObjectIdSequence const&) = delete;
  ObjectIdSequence& operator=(ObjectIdSequence const&) = delete;

 private:
  Domain& domain_;
  ObjectIdSequence* previous_;
  std::uint64_t next_;
  std::uint32_t last_;
};

template <typename T>
Ptr<T> DomainGraph::LoadCopy(ObjId ref_id, ObjId copy_id) {
  return Ptr<T>{LoadCopyImpl(ref_id, copy_id)};
}

template <typename T>
std::optional<seri::SeriResult> DomainGraph::Load(T& obj, ObjId obj_id) {
  if constexpr (std::is_same_v<T, Obj>) {
    if (load_storage_ != nullptr) {
      if (auto scope = load_storage_->ReadObjectScope(obj_id))
        obj.SetObjectScope(*scope);
    } else {
      for (auto* current = domain; current != nullptr;
           current = current->parent_domain()) {
        auto scope = current->domain_storage().ReadObjectScope(obj_id);
        if (scope) {
          obj.SetObjectScope(*scope);
          break;
        }
        // A local object without metadata is not a hole in the parent
        // namespace.
        if (!current->domain_storage().Enumerate(obj_id).empty()) break;
      }
    }
  }
  if (!cycle_detector.Add(T::kClassId, obj_id)) {
    return std::nullopt;
  }

  if constexpr (HasAnyVersionedLoad<T>::value) {
    auto result = seri::SeriResult{Error{domain_internal::missing_version}};
    auto is_missing = true;
    version_iterator<VersionLoadTrait>(
        obj, [this, obj_id, &result, &is_missing](auto version, auto& obj) {
          if (!is_missing && result.IsErr()) {
            return;
          }
          auto version_result = this->LoadVersion(version, obj, obj_id);
          is_missing = IsMissingVersion(version_result);
          if (!is_missing) {
            result = version_result;
          }
        });
    return result;
  } else {
    return LoadVersion(T::kCurrentVersion, obj, obj_id);
  }
}

struct LoadVisitor {
  void operator()(auto& v) {
    if (res) {
      res = bin_archive.Load(v);
    }
  }

  seri::BinaryArchive<DomainBuffer>& bin_archive;
  seri::SeriResult res{Ok{seri::good}};
};

template <typename T, auto V>
seri::SeriResult DomainGraph::LoadVersion(Version<V> version, T& obj,
                                          ObjId obj_id) {
  auto load = GetReader({obj_id, T::kClassId, V});
  if constexpr (!std::is_same_v<std::decay_t<T>, Obj>) {
    // A layer the node does not store is taken from its origin (one hop);
    // Obj/S0 carrying the origin is loaded before derived layers.
    if (load.result == DomainLoadResult::kEmpty) {
      ObjId const origin_id = obj.Obj::origin_id();
      if (origin_id.is_valid() && origin_id != obj_id) {
        load = GetReader({origin_id, T::kClassId, V});
      }
    }
  }
  if (load.result == DomainLoadResult::kEmpty) {
    return Error{domain_internal::missing_version};
  }
  if (load.result != DomainLoadResult::kLoaded || load.reader == nullptr) {
    return Error{seri::read_error};
  }

  if constexpr (std::is_same_v<T, Obj> && V == 0) {
    // Original Obj/S0 had no fields. The native origin reference is optional
    // when upgrading that empty base layer; derived business data still loads.
    if (load.reader->IsEmpty() == true) {
      obj.SetOriginId(ObjId{});
      return Ok{seri::good};
    }
  }

  auto bin_archive =
      seri::BinaryArchive{DomainBuffer{.id = obj_id,
                                       .domain_graph = this,
                                       .writer = {},
                                       .reader = load.reader.get()}};

  auto load_visitor = LoadVisitor{bin_archive};

  // if T has any versioned, it also must have Load for this version
  if constexpr (HasAnyVersionedLoad<T>::value) {
    auto visitor = VersionNodeVisitor{load_visitor};
    obj.Load(version, visitor);
  } else {
    // load or deserialize object
    domain_visitor::DomainVisit(obj, load_visitor);
  }
  return load_visitor.res;
}

template <typename T>
std::optional<seri::SeriResult> DomainGraph::Save(T const& obj, ObjId obj_id) {
  if (!cycle_detector.Add(T::kClassId, obj_id)) {
    return std::nullopt;
  }

  if constexpr (HasAnyVersionedSave<T>::value) {
    auto result = seri::SeriResult{Ok{seri::good}};
    version_iterator<VersionSaveTrait>(
        obj, [this, obj_id, &result](auto version, auto& obj) {
          if (result.IsErr()) {
            return;
          }
          if constexpr (requires { obj.ShouldSaveVersion(version); }) {
            // Historical scratch must include every materialized field,
            // even when a normal persistent save leaves that version owned
            // by the object's origin. Replay must not fall back to live data.
            if (serialization_scope !=
                    GraphSerializationScope::MaterializedPublication &&
                serialization_scope !=
                    GraphSerializationScope::HistoricalBaseSnapshot &&
                !obj.ShouldSaveVersion(version)) {
              return;
            }
          }
          result = this->SaveVersion(version, obj, obj_id);
        });
    return result;
  } else {
    return SaveVersion(T::kCurrentVersion, obj, obj_id);
  }
}

struct SaveVisitor {
  void operator()(auto const& v) {
    if (res) {
      res = bin_archive.Save(v);
    }
  }

  seri::BinaryArchive<DomainBuffer>& bin_archive;
  seri::SeriResult res{Ok{seri::good}};
};

template <typename T, auto V>
seri::SeriResult DomainGraph::SaveVersion(Version<V> version, T const& obj,
                                          ObjId obj_id) {
  DomainQuery query{obj_id, T::kClassId, V};
  if constexpr (requires { obj.object_scope(); }) {
    query.object_scope = obj.object_scope();
  }
  if (serialization_scope == GraphSerializationScope::LocalPersistent) {
    if constexpr (requires {
                    obj.ShouldForceMaterializeLayer(T::kClassId, V);
                  }) {
      query.force_materialize = obj.ShouldForceMaterializeLayer(T::kClassId, V);
    }
  }
  auto storage_writer = GetWriter(query);

  auto bin_archive =
      seri::BinaryArchive{DomainBuffer{.id = obj_id,
                                       .domain_graph = this,
                                       .writer = storage_writer.get(),
                                       .reader = {}}};

  auto save_visitor = SaveVisitor{bin_archive};

  if constexpr (HasAnyVersionedSave<T>::value) {
    auto visitor = VersionNodeVisitor{save_visitor};
    obj.Save(version, visitor);
  } else {
    // load or deserialize object
    domain_visitor::DomainVisit(
        obj, save_visitor,
        domain_visitor::PolicyConst<domain_visitor::VisitPolicy::kShallow>{});
  }
  if (save_visitor.res.IsErr()) return save_visitor.res;
  return storage_writer->Commit();
}

namespace seri {
template <typename T>
  requires(std::is_base_of_v<Obj, T>)
struct Serializer<BinaryArchive<DomainBuffer>, T> {
  using Archive = BinaryArchive<DomainBuffer>;

  SeriResult Seri(Archive& arch, Meta<T const> meta_val) const {
    auto result =
        arch.buffer().domain_graph->Save(meta_val.value, arch.buffer().id);
    return result.value_or(Ok{good});
  }

  SeriResult Deseri(Archive& arch, Meta<T> meta_val) const {
    // Node::Load base_ only: Seri() persists ae::Obj via Save side effects, no
    // inline bytes.
    if (arch.buffer().reader != nullptr &&
        std::is_same_v<std::decay_t<T>, Obj>) {
      return Ok{good};
    }
    auto result =
        arch.buffer().domain_graph->Load(meta_val.value, arch.buffer().id);
    return result.value_or(Ok{good});
  }
};

template <>
struct Serializer<BinaryArchive<DomainBuffer>, std::string> {
  SeriResult Deseri(BinaryArchive<DomainBuffer>& archive,
                    Meta<std::string> val) const;
  SeriResult Seri(BinaryArchive<DomainBuffer>& archive,
                  Meta<std::string const> val) const;
};

template <>
struct Serializer<BinaryArchive<DomainBuffer>, std::vector<std::uint8_t>> {
  SeriResult Deseri(BinaryArchive<DomainBuffer>& archive,
                    Meta<std::vector<std::uint8_t>> val) const;
  SeriResult Seri(BinaryArchive<DomainBuffer>& archive,
                  Meta<std::vector<std::uint8_t> const> val) const;
};

}  // namespace seri
}  // namespace ae

#endif  // AETHER_OBJECTS_OBJ_DOMAIN_H_
