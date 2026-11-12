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

#include "aether-objects/domain_storage/file_system_std_storage.h"

#if defined AE_FILE_SYSTEM_STD_ENABLED

#  include <algorithm>
#  include <atomic>
#  include <cstdio>
#  include <filesystem>
#  include <fstream>
#  include <ios>
#  include <limits>
#  include <set>
#  include <string>
#  include <system_error>

#  include "aether-objects/log.h"
#  if defined(_WIN32)
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <windows.h>
#  else
#    include <fcntl.h>
#    include <unistd.h>
#    include <cerrno>
#  endif

namespace ae {

class FstreamStorageWriter final : public IDomainStorageWriter {
 public:
  FstreamStorageWriter(DomainQuery q, std::filesystem::path target)
      : query{std::move(q)}, target_{std::move(target)} {
    std::error_code error;
    std::filesystem::create_directories(target_.parent_path(), error);
    if (error) return;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
#  if defined(_WIN32)
      auto const process = GetCurrentProcessId();
#  else
      auto const process = getpid();
#  endif
      temporary_ = target_.parent_path() /
                   ("." + target_.filename().string() + ".tmp." +
                    std::to_string(process) + "." +
                    std::to_string(next_temporary_.fetch_add(1)));
#  if defined(_WIN32)
      file_ = CreateFileW(temporary_.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file_ != INVALID_HANDLE_VALUE) return;
      if (GetLastError() != ERROR_FILE_EXISTS &&
          GetLastError() != ERROR_ALREADY_EXISTS)
        break;
#  else
      file_ = open(temporary_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                   0666);
      if (file_ >= 0) return;
      if (errno != EEXIST) break;
#  endif
    }
    // A name we did not create belongs to another writer; never remove it.
    temporary_.clear();
  }
  ~FstreamStorageWriter() override {
    Close();
    if (!committed_ && !temporary_.empty()) {
      std::error_code ignored;
      std::filesystem::remove(temporary_, ignored);
    }
  }
  seri::SeriResult Write(seri::SizeWriteTag data) override {
    if (data.size > std::numeric_limits<std::uint32_t>::max()) return Fail();
    auto const size = static_cast<std::uint32_t>(data.size);
    return Write(seri::DataTag{size});
  }
  seri::SeriResult Write(seri::DataWriteTag data) override {
    if (!Open() || failed_ || committed_) return Fail();
    auto const* bytes = static_cast<std::uint8_t const*>(data.data);
    std::size_t done = 0;
    while (done < data.size) {
#  if defined(_WIN32)
      auto const amount =
          static_cast<DWORD>(std::min<std::size_t>(data.size - done, MAXDWORD));
      DWORD written = 0;
      if (!WriteFile(file_, bytes + done, amount, &written, nullptr) ||
          written == 0)
        return Fail();
#  else
      auto const amount = std::min<std::size_t>(
          data.size - done, std::numeric_limits<ssize_t>::max());
      auto const written = write(file_, bytes + done, amount);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) return Fail();
#  endif
      done += static_cast<std::size_t>(written);
    }
    written_size += data.size;
    return Ok{seri::good};
  }
  seri::SeriResult Commit() override {
    if (committed_) return Ok{seri::good};
    if (!Open() || failed_) return Fail();
#  if defined(_WIN32)
    if (!FlushFileBuffers(file_)) return Fail();
#  else
    if (fsync(file_) != 0) return Fail();
#  endif
    if (!Close()) return Fail();
#  if defined(_WIN32)
    if (!MoveFileExW(temporary_.c_str(), target_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return Fail();
#  else
    if (rename(temporary_.c_str(), target_.c_str()) != 0) return Fail();
    // The replacement is already visible. A sync error leaves acknowledgement
    // uncertain, but the file still contains a complete old or new version.
    auto const directory =
        open(target_.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return Fail();
    auto const synced = fsync(directory) == 0;
    auto const closed = close(directory) == 0;
    if (!synced || !closed) return Fail();
#  endif
    committed_ = true;
    AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdSaved,
                        "Saved object id={}, class id={}, version={}, size={}",
                        query.id, query.class_id,
                        static_cast<int>(query.version), written_size);
    return Ok{seri::good};
  }

 private:
  bool Open() const {
#  if defined(_WIN32)
    return file_ != INVALID_HANDLE_VALUE;
#  else
    return file_ >= 0;
#  endif
  }
  bool Close() {
    if (!Open()) return true;
#  if defined(_WIN32)
    auto const saved = file_;
    file_ = INVALID_HANDLE_VALUE;
    return CloseHandle(saved) != 0;
#  else
    auto const saved = file_;
    file_ = -1;
    return close(saved) == 0;
#  endif
  }
  seri::SeriResult Fail() {
    failed_ = true;
    return Error{seri::write_error};
  }
  DomainQuery query;
  std::filesystem::path target_, temporary_;
#  if defined(_WIN32)
  HANDLE file_{INVALID_HANDLE_VALUE};
#  else
  int file_{-1};
#  endif
  bool committed_{}, failed_{};
  std::size_t written_size{};
  static inline std::atomic<std::uint64_t> next_temporary_{0};
};

class FstreamStorageReader final : public IDomainStorageReader {
 public:
  explicit FstreamStorageReader(std::ifstream&& f) : file{std::move(f)} {}
  ~FstreamStorageReader() override { file.close(); }

  seri::SeriResult Read(seri::SizeReadTag data) override {
    std::uint32_t u_size{};
    TRY_RESULT(Read(seri::DataTag{u_size}));
    data.size = static_cast<std::size_t>(u_size);
    return Ok{seri::good};
  }

  seri::SeriResult Read(seri::DataReadTag data) override {
    if (file.eof()) {
      return Error{seri::read_eof};
    }

    file.read(reinterpret_cast<std::ofstream::char_type*>(data.data),
              static_cast<std::streamsize>(data.size));
    if (file.bad()) {
      return Error{seri::read_error};
    }
    if (file.gcount() != static_cast<std::streamsize>(data.size)) {
      return Error{file.eof() ? seri::read_eof : seri::read_error};
    }
    return Ok{seri::good};
  }

 private:
  std::ifstream file;
};

FileSystemStdStorage::FileSystemStdStorage() = default;

FileSystemStdStorage::~FileSystemStdStorage() = default;

std::unique_ptr<IDomainStorageWriter> FileSystemStdStorage::Store(
    DomainQuery const& query) {
  auto class_dir = std::filesystem::path("state") /
                   std::to_string(query.id.id()) /
                   std::to_string(query.class_id);

  auto version_data_path = class_dir / std::to_string(query.version);
  return std::make_unique<FstreamStorageWriter>(query,
                                                std::move(version_data_path));
}

ClassList FileSystemStdStorage::Enumerate(const ae::ObjId& obj_id) {
  // collect unique classes
  std::set<uint32_t> classes;

  auto state_dir = std::filesystem::path{"state"};
  auto ec = std::error_code{};
  auto obj_dir = state_dir / std::to_string(obj_id.id());
  for (auto const& class_dir :
       std::filesystem::directory_iterator(obj_dir, ec)) {
    auto file_name = class_dir.path().filename().string();
    classes.insert(static_cast<std::uint32_t>(std::stoul(file_name)));
  }
  AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdEnumerated,
                      "Enumerated classes {}", classes);

  if (ec) {
    AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdEnumerateError,
                        "Unable to open directory with error {}", ec.message());
  }

  return ClassList{classes.begin(), classes.end()};
}

DomainLoad FileSystemStdStorage::Load(DomainQuery const& query) {
  auto object_dir =
      std::filesystem::path("state") / std::to_string(query.id.id());
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    return {DomainLoadResult::kEmpty, {}};
  }

  auto is_dir_empty = [&]() {
    auto iter = std::filesystem::directory_iterator{object_dir};
    return std::filesystem::begin(iter) == std::filesystem::end(iter);
  };
  if (is_dir_empty()) {
    return {DomainLoadResult::kRemoved, {}};
  }

  auto class_dir = object_dir / std::to_string(query.class_id);
  auto version_data_path = class_dir / std::to_string(query.version);
  std::ifstream f(version_data_path, std::ios::in | std::ios::binary);
  if (!f.good()) {
    AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdOpenError,
                        "Unable to open file {}", version_data_path.string());
    return DomainLoad{DomainLoadResult::kEmpty, {}};
  }

  AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdLoaded,
                      "Loaded object id={}, class id={}, version={}", query.id,
                      query.class_id, static_cast<int>(query.version));

  return {DomainLoadResult::kLoaded,
          std::make_unique<FstreamStorageReader>(std::move(f))};
}

void FileSystemStdStorage::Remove(ae::ObjId const& obj_id) {
  auto object_dir =
      std::filesystem::path("state") / std::to_string(obj_id.id());
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    std::filesystem::create_directory(object_dir);
    return;
  }
  if (ec) {
    AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdCheckDirectoryError,
                        "Unable to check if dir exists {} error {}",
                        object_dir.string(), ec.message());
    return;
  }

  for (auto const& class_dir :
       std::filesystem::directory_iterator(object_dir, ec)) {
    auto ec2 = std::error_code{};
    std::filesystem::remove_all(class_dir.path(), ec2);
    if (ec2) {
      AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdRemoveDirectoryError,
                          "Unable to remove dir {}, error {}",
                          class_dir.path().string(), ec2.message());
      continue;
    }
    AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdRemoved, "Object removed {}",
                        obj_id);
  }
  if (ec) {
    AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdRemoveError,
                        "Unable to open directory with error {}", ec.message());
  }
}

void FileSystemStdStorage::CleanUp() {
  std::filesystem::remove_all("state");
  AE_LOG_MACRO_TAGGED(::ae::objects_tele::kStdRemovedAll, "Removed all!", 0);
}
}  // namespace ae

#endif  // AE_FILE_SYSTEM_STD_ENABLED
