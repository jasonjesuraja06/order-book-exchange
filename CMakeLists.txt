cmake_minimum_required(VERSION 3.20)
project(OrderBookExchange VERSION 1.0.0 LANGUAGES CXX)

# We're using C++17 — gives us std::optional, structured bindings,
# string_view, and other modern features quant firms expect you to know
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags for release builds (-O2 = fast, -O3 = fastest)
# Debug builds get no optimization so debuggers work properly
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address")

# Tell CMake where our header files live
# This is like Python's sys.path — the compiler searches these folders
# when you write #include "core/Order.h"
include_directories(${PROJECT_SOURCE_DIR}/include)

# ---------- Core Library ----------
# This is the heart of the exchange: orders, order book, matching engine
# We compile it as a static library (.a file) so other targets can link to it
add_library(exchange_core STATIC
    src/core/Order.cpp
    src/core/OrderBook.cpp
    src/core/MatchingEngine.cpp
)

# ---------- Network Library ----------
# TCP server that accepts client connections and forwards orders
add_library(exchange_network STATIC
    src/network/TcpServer.cpp
    src/network/Protocol.cpp
)

# ---------- Simulation Library ----------
# Trading bots that generate realistic order flow
add_library(exchange_simulation STATIC
    src/simulation/MarketMaker.cpp
    src/simulation/MomentumTrader.cpp
    src/simulation/NoiseTrader.cpp
    src/simulation/Simulation.cpp
)

# ---------- Main Exchange Executable ----------
# This is what you actually run: starts the exchange server
add_executable(exchange src/main.cpp)
target_link_libraries(exchange exchange_core exchange_network exchange_simulation pthread)

# ---------- Simulation Runner ----------
# Runs a full simulated trading day without needing the TCP server
add_executable(run_simulation src/run_simulation.cpp)
target_link_libraries(run_simulation exchange_core exchange_simulation pthread)

# ---------- Google Test (Unit Tests) ----------
# FetchContent downloads GoogleTest from GitHub at build time
# No need to install it manually — CMake handles everything
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(tests
    tests/test_order.cpp
    tests/test_orderbook.cpp
    tests/test_matching_engine.cpp
)
target_link_libraries(tests exchange_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(tests)

# ---------- Google Benchmark (Performance) ----------
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(benchmarks
    benchmarks/bench_matching_engine.cpp
)
target_link_libraries(benchmarks exchange_core benchmark::benchmark)
