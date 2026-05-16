cmake_minimum_required(VERSION 3.16)
project(zethamem LANGUAGES CXX)
enable_testing()

option(ZETHADB_BUILD_TESTS "Build ZethaMEM unit tests" ON)
option(ZETHADB_BUILD_FUZZ_TARGETS "Build ZethaMEM AFL++ fuzz targets" OFF)

add_library(zethamem INTERFACE)
target_include_directories(zethamem INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(zethamem INTERFACE cxx_std_20)

if(ZETHADB_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(ZETHADB_BUILD_FUZZ_TARGETS)
    file(GLOB ZETHADB_FUZZ_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/fuzz_*.cpp"
    )

    foreach(src ${ZETHADB_FUZZ_SOURCES})
        get_filename_component(target_name ${src} NAME_WE)
        add_executable(${target_name} ${src})
        target_link_libraries(${target_name} PRIVATE zethamem)
        target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        set_target_properties(${target_name} PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
        )
        target_compile_options(${target_name} PRIVATE -O1 -g)
    endforeach()

    message(STATUS "Configured fuzz targets. For AFL++, configure with -DCMAKE_CXX_COMPILER=afl-clang++")
endif()
