cmake_minimum_required(VERSION 3.20)
project(OrderBookExchange VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address")

include_directories(${PROJECT_SOURCE_DIR}/include)

# ---------- Core Library ----------
add_library(exchange_core STATIC
    src/core/Order.cpp
    src/core/OrderBook.cpp
    src/core/MatchingEngine.cpp
)

# ---------- Network Library ----------
add_library(exchange_network STATIC
    src/network/TcpServer.cpp
    src/network/Protocol.cpp
)

# ---------- Simulation Library ----------
add_library(exchange_simulation STATIC
    src/simulation/MarketMaker.cpp
    src/simulation/MomentumTrader.cpp
    src/simulation/NoiseTrader.cpp
    src/simulation/Simulation.cpp
)
# Declared so CMake orders the archives correctly. GNU ld resolves static
# libraries in command-line order and will not go back for a symbol it has
# already passed; Apple's linker is not order-sensitive, so a missing
# dependency here links locally on macOS and fails on Linux.
target_link_libraries(exchange_simulation PUBLIC exchange_core)

# ---------- Risk Library ----------
add_library(exchange_risk STATIC
    src/risk/RiskChecker.cpp
)
target_link_libraries(exchange_risk PUBLIC exchange_core)

# ---------- Third-party: HdrHistogram_c (latency histograms) ----------
include(FetchContent)

set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_INSTALL_SHARED OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_INSTALL_STATIC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    hdr_histogram
    GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
    GIT_TAG        0.11.8
)
FetchContent_MakeAvailable(hdr_histogram)

# ---------- Metrics Library ----------
add_library(exchange_metrics STATIC
    src/metrics/LatencyHistogram.cpp
)
target_link_libraries(exchange_metrics PUBLIC hdr_histogram_static)
target_include_directories(exchange_metrics PUBLIC ${hdr_histogram_SOURCE_DIR}/include)

# ---------- Replay Library ----------
add_library(exchange_replay STATIC
    src/replay/ReplayHarness.cpp
)
target_link_libraries(exchange_replay PUBLIC exchange_core exchange_risk exchange_metrics)

target_link_libraries(exchange_network PUBLIC exchange_core exchange_risk)

# ---------- Main Exchange Executable ----------
add_executable(exchange src/main.cpp)
# Each library declares its own PUBLIC dependencies above, so the executables
# link only what they use directly and CMake propagates the rest in an order
# that satisfies GNU ld as well as Apple's order-insensitive linker.
target_link_libraries(exchange
    exchange_network exchange_simulation exchange_metrics pthread)

# ---------- Simulation Runner ----------
add_executable(run_simulation src/run_simulation.cpp)
target_link_libraries(run_simulation
    exchange_simulation exchange_metrics pthread)

# ---------- Replay Runner ----------
add_executable(run_replay src/run_replay.cpp)
target_link_libraries(run_replay exchange_replay pthread)

# ---------- Google Test ----------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(tests
    tests/test_order.cpp
    tests/test_object_pool.cpp
    tests/test_orderbook.cpp
    tests/test_matching_engine.cpp
    tests/test_risk_checker.cpp
    tests/test_latency_histogram.cpp
    tests/test_replay_harness.cpp
)
target_link_libraries(tests exchange_replay GTest::gtest_main)
target_compile_definitions(tests PRIVATE
    SAMPLE_TAPE_PATH="${CMAKE_SOURCE_DIR}/data/sample_tape.csv")
include(GoogleTest)
gtest_discover_tests(tests)

# ---------- Google Benchmark ----------
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
target_link_libraries(benchmarks
    exchange_core exchange_metrics benchmark::benchmark)

add_executable(bench_throughput
    benchmarks/bench_throughput.cpp
)
target_link_libraries(bench_throughput
    exchange_core exchange_metrics)

# ---------- Struct/wire size probe ----------
add_executable(print_sizes tools/print_sizes.cpp)
target_link_libraries(print_sizes exchange_core)
