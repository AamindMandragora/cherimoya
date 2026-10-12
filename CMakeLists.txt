cmake_minimum_required(VERSION 3.20)
project(cherimoya LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wsign-compare -Wshadow)
endif()

add_library(cheri INTERFACE)
target_include_directories(cheri INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diagnostics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parser
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sema
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver
)

add_executable(cheri_bin tools/cheri/main.cpp)
set_target_properties(cheri_bin PROPERTIES OUTPUT_NAME cheri)
target_link_libraries(cheri_bin PRIVATE cheri)

option(CHERI_BUILD_TESTS "Build unit tests" ON)
if(CHERI_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
  )
  FetchContent_MakeAvailable(Catch2)

  enable_testing()
  add_executable(cheri_tests 
    tests/unit/lexer/test_lexer.cpp
    tests/unit/parser/test_parser.cpp
    tests/unit/parser/test_printer.cpp
    tests/unit/sema/test_sema.cpp
  )
  target_link_libraries(cheri_tests PRIVATE cheri Catch2::Catch2WithMain)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(cheri_tests)
endif()