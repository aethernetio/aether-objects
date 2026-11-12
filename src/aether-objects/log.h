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
#ifndef AETHER_OBJECTS_LOG_H_
#define AETHER_OBJECTS_LOG_H_

#ifdef AE_LOGGER_INCLUDE
#  include AE_LOGGER_INCLUDE
#endif

// IWYU pragma: begin_exports
#include <iostream>
#include "aether-miscpp/format/format.h"
// IWYU pragma: end_exports

#ifndef AE_LOG_MACRO
#  define AE_OBJECTS_DEFAULT_LOGGER 1
#  ifndef AE_NO_DEBUG_LOG
#    define AE_NO_DEBUG_LOG 0
#  endif
#  if !defined NDEBUG && !AE_NO_DEBUG_LOG
#    define AE_LOG_MACRO(FORMAT_STR, ...)                             \
      do {                                                            \
        ::ae::Format(std::cout, "OBJ_SYS:[{:time}]:" FORMAT_STR "\n", \
                     std::chrono::system_clock::now() __VA_OPT__(, )  \
                         __VA_ARGS__);                                \
      } while (false)
#  else
#    define AE_LOG_MACRO(FORMAT_STR, ...)
#  endif
#endif
// An explicit tag gives every object-system callsite its own stable index.
// Custom AE_LOG_MACRO overrides keep control of logging through the old hook.
#if defined(AE_OBJECTS_RAW_TELE) && AE_OBJECTS_RAW_TELE && \
    defined(AE_OBJECTS_DEFAULT_LOGGER)
#  include "aether-objects/telemetry.h"
#  include "aether-tele/defines.h"
#  if !defined NDEBUG && !AE_NO_DEBUG_LOG
#    define AE_LOG_MACRO_TAGGED(TAG, FORMAT_STR, ...)           \
      AE_TELE_DEBUG_TO(::ae::objects_tele::ObjectTeleSink, TAG, \
                       FORMAT_STR __VA_OPT__(, ) __VA_ARGS__)
#  else
#    define AE_LOG_MACRO_TAGGED(TAG, FORMAT_STR, ...)
#  endif
#else
#  define AE_LOG_MACRO_TAGGED(TAG, FORMAT_STR, ...) \
    AE_LOG_MACRO(FORMAT_STR __VA_OPT__(, ) __VA_ARGS__)
#endif

#endif  // AETHER_OBJECTS_LOG_H_
