cmake_minimum_required(VERSION 3.16)
project(c_core C)

# Local-only build. No CI integration, no GitHub Actions wiring.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CRG_BUILD_SHARED "Build the shared library consumed by ctypes" ON)
option(CRG_BUILD_TESTS "Build and register the Unity tests" ON)
option(CRG_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# -Werror is not negotiable for ported code.
set(CRG_WARNINGS -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wformat=2)

file(GLOB CRG_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

if(CRG_BUILD_SHARED)
  add_library(c_core SHARED ${CRG_SOURCES})
else()
  add_library(c_core STATIC ${CRG_SOURCES})
endif()

target_include_directories(c_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(c_core PRIVATE CRG_C_CORE_BUILD=1)
target_compile_options(c_core PRIVATE ${CRG_WARNINGS})

set_target_properties(c_core PROPERTIES
  C_VISIBILITY_PRESET hidden
  VERSION 0.1.0
  SOVERSION 0
  OUTPUT_NAME crg_c_core)

if(CRG_SANITIZE)
  target_compile_options(c_core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -g)
  target_link_options(c_core PUBLIC -fsanitize=address,undefined)
endif()

# --- Tests (Unity, vendored) -------------------------------------------------
if(CRG_BUILD_TESTS)
  enable_testing()

  add_library(c_core_unity STATIC third_party/unity/unity.c)
  target_include_directories(c_core_unity PUBLIC third_party/unity)
  target_compile_options(c_core_unity PRIVATE -Wall -Wextra -Wno-unused-parameter)

  file(GLOB CRG_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests/c_core/test_*.c")

  foreach(test_source IN LISTS CRG_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable("${test_name}" "${test_source}")
    target_link_libraries("${test_name}" PRIVATE c_core c_core_unity)
    target_compile_options("${test_name}" PRIVATE ${CRG_WARNINGS})
    set_target_properties("${test_name}" PROPERTIES
      UNITY_INCLUDE_CONFIG_H "")
    add_test(NAME "${test_name}" COMMAND "${test_name}")
  endforeach()
endif()
