cmake_minimum_required(VERSION 3.16)
project(zethamem_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(zethamem_unit_tests unit/test_zethamem.cpp)
target_include_directories(zethamem_unit_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(zethamem_unit_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(zethamem_unit_tests)
