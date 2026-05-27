cmake_minimum_required(VERSION 3.16)
project(doost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

option(DOOST_ENABLE_SIGSEGV_HANDLER
        "Compile optional SIGSEGV/SIGBUS pool-overflow reporting support" ON)

add_library(doost_pool STATIC
        src/downward_pool.cpp
        src/downward_pool_storage.cpp
        src/nonblocking_downward_pool.cpp
)
target_include_directories(doost_pool PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(doost_pool PUBLIC cxx_std_20)
if(DOOST_ENABLE_SIGSEGV_HANDLER)
        target_compile_definitions(doost_pool PUBLIC DOOST_ENABLE_SIGSEGV_HANDLER=1)
endif()

add_library(doost_string STATIC
        src/string.cpp
)
target_include_directories(doost_string PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(doost_string PUBLIC cxx_std_20)

add_library(doost_safe_memory STATIC
        src/safe_memory.cpp
)
target_include_directories(doost_safe_memory PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(doost_safe_memory PUBLIC cxx_std_20)

add_library(doost_parallel_memcpy STATIC
        src/parallel_memcpy.cpp
)
target_include_directories(doost_parallel_memcpy PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(doost_parallel_memcpy PUBLIC cxx_std_20)
target_link_libraries(doost_parallel_memcpy PUBLIC Threads::Threads)

add_executable(pool_baseline benchmark/pool_baseline.cpp)

add_executable(pool_allocator_benchmark benchmark/pool_allocator_benchmark.cpp)
target_link_libraries(pool_allocator_benchmark PRIVATE doost_pool)

add_executable(standard_allocator_benchmark
        benchmark/standard_allocator_benchmark.cpp
)
target_link_libraries(standard_allocator_benchmark PRIVATE
        doost_pool
        Threads::Threads
)

add_executable(global_mutex_pool_benchmark
        benchmark/global_mutex_pool_benchmark.cpp
)
target_link_libraries(global_mutex_pool_benchmark PRIVATE
        doost_pool
        Threads::Threads
)

add_executable(global_nonblocking_pool_benchmark
        benchmark/global_nonblocking_pool_benchmark.cpp
)
target_link_libraries(global_nonblocking_pool_benchmark PRIVATE
        doost_pool
        Threads::Threads
)

add_executable(thread_local_pool_benchmark
        benchmark/thread_local_pool_benchmark.cpp
)
target_link_libraries(thread_local_pool_benchmark PRIVATE
        doost_pool
        Threads::Threads
)

add_executable(string_benchmark
        benchmark/string_benchmark.cpp
)
target_link_libraries(string_benchmark PRIVATE doost_string)

add_executable(parallel_memcpy_benchmark
        benchmark/parallel_memcpy_benchmark.cpp
)
target_link_libraries(parallel_memcpy_benchmark PRIVATE doost_parallel_memcpy)

if(BUILD_TESTING)
        add_executable(doost_tests test/doost_tests.cpp)
        target_link_libraries(doost_tests PRIVATE doost_pool Threads::Threads)
        add_test(NAME doost_tests COMMAND doost_tests)

        add_executable(string_tests test/string_tests.cpp)
        target_link_libraries(string_tests PRIVATE doost_string)
        add_test(NAME string_tests COMMAND string_tests)

        add_executable(safe_memory_tests test/safe_memory_tests.cpp)
        target_link_libraries(safe_memory_tests PRIVATE doost_safe_memory)
        add_test(NAME safe_memory_tests COMMAND safe_memory_tests)

        add_executable(parallel_memcpy_tests test/parallel_memcpy_tests.cpp)
        target_link_libraries(parallel_memcpy_tests PRIVATE doost_parallel_memcpy)
        add_test(NAME parallel_memcpy_tests COMMAND parallel_memcpy_tests)
endif()
