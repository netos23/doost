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
        src/mutex_downward_pool.cpp
        src/nonblocking_downward_pool.cpp
)
target_include_directories(doost_pool PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(doost_pool PUBLIC cxx_std_20)
if(DOOST_ENABLE_SIGSEGV_HANDLER)
        target_compile_definitions(doost_pool PUBLIC DOOST_ENABLE_SIGSEGV_HANDLER=1)
endif()

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

if(BUILD_TESTING)
        add_executable(doost_tests test/doost_tests.cpp)
        target_link_libraries(doost_tests PRIVATE doost_pool Threads::Threads)
        add_test(NAME doost_tests COMMAND doost_tests)
endif()
