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

cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(aether-objects VERSION 1.0.0 LANGUAGES CXX)

# define is the project is the current root project
# root project builds tests and defines installation targets by default
if( ${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
  set(IS_ROOT_PROJECT ON)
else()
  set(IS_ROOT_PROJECT OFF)
endif()

option(AE_BUILD_TESTS "Build tests" ${IS_ROOT_PROJECT} )
option(AE_INSTALL "Install aether-objects library" ${IS_ROOT_PROJECT} )
option(AE_NO_DEBUG_LOG "Do not print debug logs" !${IS_ROOT_PROJECT} )
set(AE_LOGGER_INCLUDE "" CACHE STRING "Custom header for logger")
option(AE_OBJECTS_RAW_TELE "Record object debug logs in an independent raw telemetry space" OFF)

include(cmake/CPM.cmake)

CPMAddPackage(
  NAME aether-miscpp
  GIT_REPOSITORY "https://github.com/aethernetio/aether-miscpp.git"
  GIT_TAG "6839624442621a5b060ae610e91f480278ebaf6e"
  OPTIONS "AE_INSTALL ${AE_INSTALL}" "AE_BUILD_TESTS ${AE_BUILD_TESTS}"
  EXCLUDE_FROM_ALL FALSE
)

if(AE_OBJECTS_RAW_TELE)
  CPMAddPackage(
    NAME aether-tele
    GIT_REPOSITORY "https://github.com/aethernetio/aether-tele.git"
    GIT_TAG "7d14cd3cb8261ca78fe88c7e1370ff9ae3060e52"
    OPTIONS "AE_TELE_INSTALL ${AE_INSTALL}" "AE_TELE_BUILD_TESTS OFF"
    EXCLUDE_FROM_ALL FALSE
  )
endif()

add_library(${PROJECT_NAME} STATIC)
add_library(aether::objects ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
  src/aether-objects/ptr/ptr.cpp
  src/aether-objects/ptr/ptr_view.cpp
  src/aether-objects/ptr/ref_tree.cpp

  src/aether-objects/obj/domain.cpp
  src/aether-objects/obj/obj.cpp
  src/aether-objects/obj/obj_id.cpp
  src/aether-objects/obj/obj_ptr_base.cpp
  src/aether-objects/obj/registry.cpp

  src/aether-objects/domain_storage/file_system_std_storage.cpp
  src/aether-objects/domain_storage/ram_domain_storage.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(${PROJECT_NAME} PUBLIC aether::miscpp)
if(AE_OBJECTS_RAW_TELE)
  target_link_libraries(${PROJECT_NAME} PUBLIC aether::tele)
  target_compile_definitions(${PROJECT_NAME} PUBLIC AE_OBJECTS_RAW_TELE=1
    $<$<CONFIG:Debug>:AE_TELE_RAW_DEBUG=1>)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(${PROJECT_NAME} PUBLIC /Zc:preprocessor)
endif()

if(NOT "${AE_LOGGER_INCLUDE}" STREQUAL "")
  target_compile_definitions(${PROJECT_NAME} PUBLIC "AE_LOGGER_INCLUDE=${AE_LOGGER_INCLUDE}")
endif()
if(AE_NO_DEBUG_LOG)
  target_compile_definitions(${PROJECT_NAME} PUBLIC "AE_NO_DEBUG_LOG=1")
endif()

if(AE_BUILD_TESTS)
  enable_testing()
  add_subdirectory("tests")
endif()

if(AE_INSTALL)
  include(GNUInstallDirs)

  # define the installation target, install lib headers and export cmake config
  install(TARGETS ${PROJECT_NAME}
          EXPORT ${PROJECT_NAME}Targets
  )

  install(DIRECTORY src/aether-objects DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT ${PROJECT_NAME}Targets
          FILE ${PROJECT_NAME}Targets.cmake
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
  )
endif()
