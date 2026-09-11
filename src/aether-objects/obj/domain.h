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
#include <optional>
#include <set>
#include <type_traits>

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"

#include "aether-objects/ptr/ptr_view.h"

#include "aether-objects/env/env.h"
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

  std::set<Node> visited_nodes;
};

struct DomainBuffer {
  seri::SeriResult Write(seri::SizeWriteTag data) const {
    if (writer == nullptr) {
      return Error{seri::write_error};
    }
    return writer->Write(data);
  }
  seri::SeriResult Write(seri::DataWriteTag data) const {
    if (writer == nullptr) {
      return Error{seri::write_error};
    }
    return writer->Write(data);
  }

  seri::SeriResult Read(seri::SizeReadTag data) const {
    if (reader == nullptr) {
      return Error{seri::read_error};
    }
    return reader->Read(data);
  }
  seri::SeriResult Read(seri::DataReadTag data) const {
    if (reader == nullptr) {
      return Error{seri::read_error};
    }
    return reader->Read(data);
  }

  ObjId id;
  DomainGraph* domain_graph{};
  IDomainStorageWriter* writer{nullptr};
  IDomainStorageReader* reader{nullptr};
};

/**
 * \brief Operations on graph of objects in domain.
 * For load/save a new graph create a new DomainGraph.
 */
class DomainGraph {
 public:
  explicit DomainGraph(Domain* domain);

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

  Domain* domain{};
  DomainCycleDetector cycle_detector{};

 private:
  DomainLoad GetReader(DomainQuery const& query);
  std::unique_ptr<IDomainStorageWriter> GetWriter(DomainQuery const& query);
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

  // Search for the object by obj_id.
  Ptr<Obj> Find(ObjId obj_id) const;

  void AddObject(ObjId id, Ptr<Obj> const& obj);
  void RemoveObject(Obj* obj);

 private:
  Ptr<Obj> ConstructObj(Factory const& factory, ObjId id);

  bool IsLast(std::uint32_t class_id) const;
  bool IsExisting(std::uint32_t class_id) const;

  Factory* FindClassFactory(std::uint32_t class_id);
  Factory* GetMostRelatedFactory(ObjId id);

  IDomainStorage* storage_;
  Registry* registry_;
  Env* env_{};

  std::map<ObjId::Type, PtrView<Obj>> id_objects_;
};

template <typename T>
Ptr<T> DomainGraph::LoadCopy(ObjId ref_id, ObjId copy_id) {
  return Ptr<T>{LoadCopyImpl(ref_id, copy_id)};
}

template <typename T>
std::optional<seri::SeriResult> DomainGraph::Load(T& obj, ObjId obj_id) {
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
  if (load.result == DomainLoadResult::kEmpty) {
    return Error{domain_internal::missing_version};
  }
  if (load.result != DomainLoadResult::kLoaded || load.reader == nullptr) {
    return Error{seri::read_error};
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
  auto storage_writer = GetWriter({obj_id, T::kClassId, V});

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
  return save_visitor.res;
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
