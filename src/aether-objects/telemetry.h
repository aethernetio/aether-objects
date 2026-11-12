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
#ifndef AETHER_OBJECTS_TELEMETRY_H_
#define AETHER_OBJECTS_TELEMETRY_H_

#if defined(AE_OBJECTS_RAW_TELE) && AE_OBJECTS_RAW_TELE

#  include <cstdint>
#  include <string_view>

#  include "aether-tele/configs/all_enabled.h"
#  include "aether-tele/raw/sink.h"
#  include "aether-tele/tags.h"

namespace ae::objects_tele {
// The debug-only macro gate lives in log.h. The sink itself remains available
// in release builds so an application can restore/export previously saved data.
struct Identity {
  static constexpr std::string_view kName = "objects";
  // Existing IDs retain their meanings. Bump this revision if an ID is reused.
  static constexpr std::string_view CatalogId() { return "objects-v1"; }
  static constexpr std::uint32_t ResolveIndex(std::string_view, std::uint32_t,
                                              tele::Tag const& tag) {
    return tag.index();
  }
};
using ObjectTeleSink = tele::RawTeleSink<Identity, tele::AllEnabledConfig>;
inline constexpr tele::Module kModule{1, 0, 65535, "objects"};

// Explicit callsite indices remain stable across translation units and boots.
// Distinct calls use distinct tags, even where their format text is identical.
inline constexpr tele::Tag kStdSaved{1, kModule, "StdSaved"};
inline constexpr tele::Tag kStdEnumerated{2, kModule, "StdEnumerated"};
inline constexpr tele::Tag kStdEnumerateError{3, kModule, "StdEnumerateError"};
inline constexpr tele::Tag kStdOpenError{4, kModule, "StdOpenError"};
inline constexpr tele::Tag kStdLoaded{5, kModule, "StdLoaded"};
inline constexpr tele::Tag kStdCheckDirectoryError{6, kModule,
                                                   "StdCheckDirectoryError"};
inline constexpr tele::Tag kStdRemoveDirectoryError{7, kModule,
                                                    "StdRemoveDirectoryError"};
inline constexpr tele::Tag kStdRemoved{8, kModule, "StdRemoved"};
inline constexpr tele::Tag kStdRemoveError{9, kModule, "StdRemoveError"};
inline constexpr tele::Tag kStdRemovedAll{10, kModule, "StdRemovedAll"};
inline constexpr tele::Tag kRamNotFound{11, kModule, "RamNotFound"};
inline constexpr tele::Tag kRamEnumerated{12, kModule, "RamEnumerated"};
inline constexpr tele::Tag kRamObjectMissing{13, kModule, "RamObjectMissing"};
inline constexpr tele::Tag kRamClassMissing{14, kModule, "RamClassMissing"};
inline constexpr tele::Tag kRamVersionMissing{15, kModule, "RamVersionMissing"};
inline constexpr tele::Tag kRamLoaded{16, kModule, "RamLoaded"};
inline constexpr tele::Tag kRamRemoved{17, kModule, "RamRemoved"};
inline constexpr tele::Tag kRamSaved{18, kModule, "RamSaved"};
inline constexpr tele::Tag kDomainEnumeratedNames{19, kModule,
                                                  "DomainEnumeratedNames"};
inline constexpr tele::Tag kDomainEnumeratedClasses{20, kModule,
                                                    "DomainEnumeratedClasses"};
inline constexpr tele::Tag kRegistryEntry{21, kModule, "RegistryEntry"};
}  // namespace ae::objects_tele

#endif  // AE_OBJECTS_RAW_TELE

#endif  // AETHER_OBJECTS_TELEMETRY_H_
