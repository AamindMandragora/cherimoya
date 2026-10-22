cmake_minimum_required(VERSION 3.20)
project(cherimoya LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wsign-compare -Wshadow)
endif()

# finds LLVM through llvm-config, since distributions install it under prefixes cmake doesn't search, and LLVM only accepts its own major.minor
execute_process(COMMAND llvm-config --cmakedir OUTPUT_VARIABLE LLVM_CMAKE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
find_package(LLVM 21.1 REQUIRED CONFIG HINTS ${LLVM_CMAKE_DIR})

# LLVM's headers are marked SYSTEM so their warnings stay out of the build, and only the targets that use LLVM link it
add_library(cheri_llvm INTERFACE)
target_include_directories(cheri_llvm SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(cheri_llvm INTERFACE ${LLVM_DEFINITIONS})
target_link_libraries(cheri_llvm INTERFACE LLVM)

add_library(cheri INTERFACE)
target_include_directories(cheri INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sema
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsp
)

# the runtime every program links with, whose path is baked into cheri so builds work from any directory
add_library(cheri_rt STATIC runtime/runtime.c runtime/fp8.c)
add_executable(cheri_bin tools/cheri/main.cpp)
set_target_properties(cheri_bin PROPERTIES OUTPUT_NAME cheri)
target_link_libraries(cheri_bin PRIVATE cheri cheri_llvm)
target_compile_definitions(cheri_bin PRIVATE CHERI_RUNTIME="$<TARGET_FILE:cheri_rt>")
add_dependencies(cheri_bin cheri_rt)

option(CHERI_BUILD_TESTS "build the unit tests and the program tests" ON)
if(CHERI_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
  )
  FetchContent_MakeAvailable(Catch2)

  enable_testing()
  # every unit test, where the runtime's FP8 conversions are compiled in directly, without the runtime's main, and checked against LLVM's APFloat, which is why the tests link LLVM
  add_executable(cheri_tests
    tests/unit/lexer/test_lexer.cpp
    tests/unit/parser/test_parser.cpp
    tests/unit/parser/test_printer.cpp
    tests/unit/sema/test_sema.cpp
    tests/unit/runtime/test_fp8.cpp
    tests/unit/lsp/test_lsp.cpp
    runtime/fp8.c
  )
  target_link_libraries(cheri_tests PRIVATE cheri cheri_llvm Catch2::Catch2WithMain)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(cheri_tests)

  # every program in tests/run is built with cheri and run, and its exit code checked against its first line
  file(GLOB RUN_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/run/*.moya)
  foreach(test ${RUN_TESTS})
    get_filename_component(name ${test} NAME_WE)
    add_test(NAME "Run: ${name}"
             COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_test.sh
    $<TARGET_FILE:cheri_bin> ${test}
    ${CMAKE_CURRENT_BINARY_DIR}/run)
  endforeach()
endif()
