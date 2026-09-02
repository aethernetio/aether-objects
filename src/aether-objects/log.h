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
#endif  // AETHER_OBJECTS_LOG_H_
