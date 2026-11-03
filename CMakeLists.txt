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

# the gdb printers are Python in a file of their own, which becomes a header holding its text as a raw string, made again whenever the file changes, where the text can't hold the string's closing delimiter
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen/printers.py CHERI_GDB_PRINTERS)
if(CHERI_GDB_PRINTERS MATCHES "\\)py\"")
  message(FATAL_ERROR "src/codegen/printers.py can't contain )py\", which ends the raw string it's embedded in")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/codegen/printers.py)
configure_file(src/codegen/printers.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/printers.hpp @ONLY)

add_library(cheri INTERFACE)
target_include_directories(cheri INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sema
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codegen
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lsp
)

# the runtime every program and test binary links with, and the C entry point of each, a program's or a test binary's, whose paths are baked into cheri so builds work from any directory
add_library(cheri_rt STATIC runtime/runtime.c runtime/fp8.c)
add_library(cheri_rt_start STATIC runtime/start.c)
add_library(cheri_rt_test STATIC runtime/test.c)
add_executable(cheri_bin tools/cheri/main.cpp)
set_target_properties(cheri_bin PROPERTIES OUTPUT_NAME cheri)
target_link_libraries(cheri_bin PRIVATE cheri cheri_llvm)
target_compile_definitions(cheri_bin PRIVATE
  CHERI_RUNTIME="$<TARGET_FILE:cheri_rt>"
  CHERI_START="$<TARGET_FILE:cheri_rt_start>"
  CHERI_TEST="$<TARGET_FILE:cheri_rt_test>")
add_dependencies(cheri_bin cheri_rt cheri_rt_start cheri_rt_test)

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
  # the unit tests, the programs in tests/run, and cheri test's own check are the basics, which every feature test needs to pass first, since a feature test checks itself with assert and = and runs under cheri test, so a broken basic would make its results mean nothing, and ctest leaves a feature test unrun when any of them fails
  catch_discover_tests(cheri_tests PROPERTIES FIXTURES_SETUP basics)

  # every program in tests/run is built with cheri and run, and its exit code checked against its first line
  file(GLOB RUN_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/run/*.moya)
  foreach(test ${RUN_TESTS})
    get_filename_component(name ${test} NAME_WE)
    add_test(NAME "Run: ${name}"
             COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_test.sh
    $<TARGET_FILE:cheri_bin> ${test}
    ${CMAKE_CURRENT_BINARY_DIR}/run)
    set_tests_properties("Run: ${name}" PROPERTIES FIXTURES_SETUP basics)
  endforeach()

  # cheri test runs every .toya file under tests/test, which hold every outcome a test can have, and has to print exactly what tests/test/expected.out holds
  add_test(NAME "Test: cheri test"
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_test.sh $<TARGET_FILE:cheri_bin> tests/test tests/test/expected.out
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties("Test: cheri test" PROPERTIES FIXTURES_SETUP basics)

  # every .toya file in tests/features checks a part of the language, one test per behavior, run by cheri test once the basics pass
  file(GLOB FEATURE_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/features/*.toya)
  foreach(test ${FEATURE_TESTS})
    get_filename_component(name ${test} NAME_WE)
    add_test(NAME "Feature: ${name}"
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/feature_test.sh $<TARGET_FILE:cheri_bin> ${test})
    set_tests_properties("Feature: ${name}" PROPERTIES FIXTURES_REQUIRED basics)
  endforeach()

  # every program in tests/debug is built with debug info, which llvm-dwarfdump checks, and debugged in gdb when it's installed
  find_program(LLVM_DWARFDUMP llvm-dwarfdump HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)
  file(GLOB DEBUG_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/debug/*.moya)
  foreach(test ${DEBUG_TESTS})
    get_filename_component(name ${test} NAME_WE)
    add_test(NAME "Debug: ${name}"
             COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/debug_test.sh
    $<TARGET_FILE:cheri_bin> ${test}
    ${CMAKE_CURRENT_BINARY_DIR}/debug ${LLVM_DWARFDUMP})
  endforeach()

  # the tree-sitter grammar in tree-sitter has to parse every program under tests without an error and pass its corpus tests, which is skipped when its node_modules isn't installed
  add_test(NAME "Grammar: tree-sitter"
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/grammar_test.sh
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
