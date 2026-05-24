cmake_minimum_required(VERSION 3.16)
project(doost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(doost_pool STATIC
        src/downward_pool.cpp
)
target_include_directories(doost_pool PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(doost_pool PUBLIC cxx_std_20)

add_executable(pool_baseline benchmark/pool_baseline.cpp)

add_executable(pool_allocator_benchmark benchmark/pool_allocator_benchmark.cpp)
target_link_libraries(pool_allocator_benchmark PRIVATE doost_pool)

if(BUILD_TESTING)
        add_executable(doost_tests test/doost_tests.cpp)
        target_link_libraries(doost_tests PRIVATE doost_pool)
        add_test(NAME doost_tests COMMAND doost_tests)
endif()
