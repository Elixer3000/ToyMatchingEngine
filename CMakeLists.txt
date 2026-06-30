cmake_minimum_required(VERSION 3.14)
project(matching_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

add_executable(matching_engine
    src/api/main.cpp
    src/engine/OrderBook.cpp
)

target_include_directories(matching_engine PRIVATE src src/api src/engine src/utils ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(matching_engine PRIVATE ASIO_STANDALONE)
target_link_libraries(matching_engine PRIVATE Threads::Threads)
