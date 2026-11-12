# Copyright 2026 Aethernet Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required( VERSION 3.16 )

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
set(INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../src")

include(../cmake/CPM.cmake)

# enable doubles in unity tests
add_compile_definitions("UNITY_INCLUDE_DOUBLE")
CPMAddPackage("https://github.com/ThrowTheSwitch/Unity.git#master")

add_subdirectory(test-ptr)
add_subdirectory(test-object-system)


if(AE_OBJECTS_RAW_TELE)
  foreach(mode debug disabled custom)
    add_executable(test-object-telemetry-${mode} test-telemetry/main.cpp)
    target_link_libraries(test-object-telemetry-${mode} PRIVATE aether::objects unity)
    target_compile_definitions(test-object-telemetry-${mode} PRIVATE AE_OBJECTS_TEST_${mode}=1 AE_TELE_RAW_DEBUG=1)
    add_test(NAME test-object-telemetry-${mode} COMMAND test-object-telemetry-${mode})
  endforeach()
endif()
