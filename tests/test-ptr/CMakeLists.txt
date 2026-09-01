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

option(AE_RC_PTR_BENCH "Make a benchmark for rc ptr compared to std::shared_ptr" Off)

list(APPEND test_srcs
    main.cpp
    test-ptr.cpp
    test-ptr-inheritance.cpp
    test-ptr-view.cpp
    test-ptr-cycles.cpp
    test-ref-tree.cpp
)

if(NOT CM_PLATFORM)
  project(test-ptr LANGUAGES CXX)

  add_executable(${PROJECT_NAME})
  target_sources(${PROJECT_NAME} PRIVATE ${test_srcs})
  target_link_libraries(${PROJECT_NAME} PRIVATE
    unity
    aether::miscpp
    aether::objects
  )

  add_test(NAME ${PROJECT_NAME} COMMAND $<TARGET_FILE:${PROJECT_NAME}>)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PUBLIC /Zc:preprocessor)
  endif()
else()
  message(WARNING "Not implemented for ${CM_PLATFORM}")
endif()
