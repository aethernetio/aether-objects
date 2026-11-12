/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include "aether-objects/domain_storage/file_system_std_storage.h"
#if defined(AE_FILE_SYSTEM_STD_ENABLED)
#  include <unity.h>
#  include <array>
#  include <chrono>
#  include <filesystem>
#  include <fstream>
#  include <memory>
#  include <stdexcept>
#  include "aether-objects/obj/domain.h"
#  include "objects/lifecycle.h"

namespace ae::test_atomic_storage {
class Directory {
 public:
  Directory()
      : previous_{std::filesystem::current_path()},
        path_{std::filesystem::temp_directory_path() /
              ("ae-atomic-storage-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))} {
    if (!std::filesystem::create_directory(path_))
      throw std::runtime_error("fixture directory exists");
    std::filesystem::current_path(path_);
  }
  ~Directory() {
    std::error_code ignored;
    std::filesystem::current_path(previous_, ignored);
    if (path_.parent_path() == std::filesystem::temp_directory_path())
      std::filesystem::remove_all(path_, ignored);
  }

 private:
  std::filesystem::path previous_, path_;
};
void test_CommitReplacesWholeVersionAndAbortKeepsOldBytes() {
  Directory directory;
  FileSystemStdStorage storage;
  DomainQuery query{ObjId{123}, 456, 0};
  std::array<std::uint8_t, 4> original{1, 2, 3, 4};
  {
    auto writer = storage.Store(query);
    TEST_ASSERT_FALSE(
        writer->Write(seri::DataWriteTag{original.data(), original.size()})
            .IsErr());
    TEST_ASSERT_FALSE(writer->Commit().IsErr());
    TEST_ASSERT_FALSE(writer->Commit().IsErr());
  }
  {
    auto writer = storage.Store(query);
    std::array<std::uint8_t, 2> partial{9, 8};
    TEST_ASSERT_FALSE(
        writer->Write(seri::DataWriteTag{partial.data(), partial.size()})
            .IsErr());
    // No Commit: serializer failure/destruction must not truncate old state.
  }
  auto load = storage.Load(query);
  TEST_ASSERT_TRUE(load.result == DomainLoadResult::kLoaded);
  std::array<std::uint8_t, 4> bytes{};
  TEST_ASSERT_FALSE(
      load.reader->Read(seri::DataReadTag{bytes.data(), bytes.size()}).IsErr());
  TEST_ASSERT_EQUAL_MEMORY(original.data(), bytes.data(), original.size());
  load.reader.reset();
  std::array<std::uint8_t, 5> replacement{9, 8, 7, 6, 5};
  {
    auto writer = storage.Store(query);
    TEST_ASSERT_FALSE(
        writer
            ->Write(seri::DataWriteTag{replacement.data(), replacement.size()})
            .IsErr());
    TEST_ASSERT_FALSE(writer->Commit().IsErr());
  }
  load = storage.Load(query);
  TEST_ASSERT_TRUE(load.result == DomainLoadResult::kLoaded);
  std::array<std::uint8_t, 5> restored{};
  TEST_ASSERT_FALSE(
      load.reader->Read(seri::DataReadTag{restored.data(), restored.size()})
          .IsErr());
  TEST_ASSERT_EQUAL_MEMORY(replacement.data(), restored.data(),
                           replacement.size());
  load.reader.reset();
  auto class_path = std::filesystem::path("state") / "123" / "456";
  unsigned entries = 0;
  for (auto const& entry : std::filesystem::directory_iterator(class_path)) {
    ++entries;
    TEST_ASSERT_TRUE(entry.path().filename() == "0");
  }
  TEST_ASSERT_EQUAL_UINT(1, entries);
  // A process dying before Commit can leave this sibling. Readers and class
  // enumeration must ignore it, and another writer must not erase it.
  auto orphan = class_path / ".0.tmp.crashed-writer";
  {
    std::ofstream partial(orphan, std::ios::binary);
    partial << "partial";
  }
  auto classes = storage.Enumerate(query.id);
  TEST_ASSERT_EQUAL_UINT(1, classes.size());
  TEST_ASSERT_EQUAL_UINT(456, classes[0]);
  load = storage.Load(query);
  TEST_ASSERT_TRUE(load.result == DomainLoadResult::kLoaded);
  TEST_ASSERT_FALSE(
      load.reader->Read(seri::DataReadTag{restored.data(), restored.size()})
          .IsErr());
  TEST_ASSERT_EQUAL_MEMORY(replacement.data(), restored.data(),
                           replacement.size());
  load.reader.reset();
  {
    auto writer = storage.Store(query);
    TEST_ASSERT_FALSE(
        writer
            ->Write(seri::DataWriteTag{replacement.data(), replacement.size()})
            .IsErr());
    TEST_ASSERT_FALSE(writer->Commit().IsErr());
  }
  TEST_ASSERT_TRUE(std::filesystem::exists(orphan));
}
class PartialWriter final : public IDomainStorageWriter {
 public:
  explicit PartialWriter(std::unique_ptr<IDomainStorageWriter> writer)
      : writer_{std::move(writer)} {}
  seri::SeriResult Write(seri::SizeWriteTag tag) override {
    return writer_->Write(tag);
  }
  seri::SeriResult Write(seri::DataWriteTag tag) override {
    auto result = writer_->Write(tag);
    if (result.IsErr()) return result;
    return Error{seri::write_error};
  }
  seri::SeriResult Commit() override { return writer_->Commit(); }

 private:
  std::unique_ptr<IDomainStorageWriter> writer_;
};
class PartialStorage final : public FileSystemStdStorage {
 public:
  bool fail{};
  std::unique_ptr<IDomainStorageWriter> Store(
      DomainQuery const& query) override {
    auto writer = FileSystemStdStorage::Store(query);
    if (fail && query.class_id == LifecycleObject::kClassId)
      return std::make_unique<PartialWriter>(std::move(writer));
    return writer;
  }
};
void test_PartialDomainSerializationPreservesLastCommittedObject() {
  Directory directory;
  PartialStorage storage;
  Domain domain{storage};
  auto object = LifecycleObject::ptr::Create(CreateWith{domain}.with_id(1));
  object->persisted_value = 17;
  object.Save();
  storage.fail = true;
  LifecycleObject::ResetCallbacks();
  object->persisted_value = 23;
  object.Save();
  TEST_ASSERT_EQUAL_INT(0, LifecycleObject::saved_count);
  Domain reopened{storage};
  auto loaded = LifecycleObject::ptr::Declare(CreateWith{reopened}.with_id(1));
  loaded.Load();
  TEST_ASSERT_TRUE(loaded.is_loaded());
  TEST_ASSERT_EQUAL_INT(17, loaded->persisted_value);
}
void test_DomainSaveCommitsBeforeSavedAndReloads() {
  Directory directory;
  FileSystemStdStorage storage;
  Domain domain{storage};
  LifecycleObject::ResetCallbacks();
  auto object = LifecycleObject::ptr::Create(CreateWith{domain}.with_id(1));
  object->persisted_value = 17;
  object.Save();
  TEST_ASSERT_EQUAL_INT(1, LifecycleObject::saved_count);
  Domain reopened{storage};
  auto loaded = LifecycleObject::ptr::Declare(CreateWith{reopened}.with_id(1));
  loaded.Load();
  TEST_ASSERT_TRUE(loaded.is_loaded());
  TEST_ASSERT_EQUAL_INT(17, loaded->persisted_value);
}
}  // namespace ae::test_atomic_storage
#endif
int test_atomic_storage() {
#if defined(AE_FILE_SYSTEM_STD_ENABLED)
  UNITY_BEGIN();
  RUN_TEST(ae::test_atomic_storage::
               test_PartialDomainSerializationPreservesLastCommittedObject);
  RUN_TEST(ae::test_atomic_storage::
               test_CommitReplacesWholeVersionAndAbortKeepsOldBytes);
  RUN_TEST(
      ae::test_atomic_storage::test_DomainSaveCommitsBeforeSavedAndReloads);
  return UNITY_END();
#else
  return 0;
#endif
}
