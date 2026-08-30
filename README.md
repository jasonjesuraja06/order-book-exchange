# Limit Order Book Exchange

A price-time priority matching engine in C++17 with a pre-trade risk layer, a binary TCP front end, a deterministic tape-replay harness, and an agent-based market simulation.

[![CI](https://github.com/jasonjesuraja06/order-book-exchange/actions/workflows/ci.yml/badge.svg)](https://github.com/jasonjesuraja06/order-book-exchange/actions/workflows/ci.yml)

## Correctness first

58 GoogleTest cases assert observable behavior rather than smoke-testing. They check the exact order IDs chosen by time priority at a shared price level, the per-level execution prices of a multi-level sweep, that an IOC remainder is cancelled instead of rested, each `RiskChecker` reject reason against its limit, and an end-to-end tape replay against expected trade output. Run them with `./build/tests`.

## Why it is built this way

Matching is a latency problem with a hard correctness constraint: the queue position a participant earned by arriving first has to survive every operation on the book. That pushes toward structures where cancel does not scan. Orders rest in `std::list` FIFO queues under a `std::map` of price levels, and a separate `unordered_map` from order ID to `{side, price, list iterator}` lets a cancel splice an order out in O(1) without walking its level.

That design hands raw `Order*` to the book and expects them to stay valid for the life of the order, which is what makes the allocator choice load-bearing rather than cosmetic.

## Architecture

```
 clients ──TCP──> TcpServer ──> RiskChecker ──> MatchingEngine ──> OrderBook
                (thread per       (pre-trade      (mutex, one       (bid/ask maps,
                 connection)       limits)         order at a time)  FIFO per level)
                                                         │
                                        trade callback ──┴──> RiskChecker.on_fill
                                                               (positions, notional)

 ReplayHarness (CSV tape) ──> RiskChecker ──> MatchingEngine ──> LatencyHistogram
 Simulation (market maker, momentum, noise) ──> MatchingEngine
```

Risk runs on two paths: the TCP server checks every `NewOrder` message before the engine sees it, and the replay harness checks every tape event. The in-process `Simulation` agents call the engine directly and are not risk-checked.

## Measured results

Apple M4 Pro (14 cores), 48 GB RAM, macOS arm64, Apple clang, `-DCMAKE_BUILD_TYPE=Release` (`-O3`). Raw output for every row is committed under `results/`. The throughput harness was run twice and both runs are committed, because a single wall-clock reading on a laptop is not a stable number.

| Metric | Measured | Reproduce |
|---|---|---|
| Sustained throughput | 9.01M and 10.08M ops/sec across two runs of 5,000,000 ops | `./build/bench_throughput 5000000` |
| Latency p50 / p99 / p99.9 | 83 / 500 / 583 ns and 83 / 459 / 500 ns | same two runs, phase 2 |
| Latency mean / max | 105 ns / 276,991 ns and 96 ns / 247,679 ns | same two runs, phase 2 |
| Limit insert, no match | 86.7 ns (11.53M/sec) | `./build/benchmarks --benchmark_min_time=1s` |
| Limit match, one trade | 260 ns per 2 orders (7.71M orders/sec) | same |
| Market order fill | 274 ns (3.65M/sec) | same |
| Cancel by ID | 47.9 ns (20.89M/sec) | same |
| Marginal cost per swept level | ~73 ns | same, `BM_MultiLevelSweep` 1 vs 1000 |
| `sizeof(Order)` | 40 bytes, alignment 8 | `./build/print_sizes` |
| Wire messages | 23 / 17 / 26 bytes | same |
| Unit tests | 58 passing | `./build/tests` |
| Sample tape replay | 51 events, 30 trades, 47 risk checks | `./build/run_replay data/sample_tape.csv` |

Throughput and latency are measured separately and neither is derived from the other. Phase 1 of `bench_throughput` runs a mixed workload (35% passive buy, 35% passive sell, 20% cancel, 10% IOC cross, resting depth capped at 50,000 orders) with no instrumentation and divides the operation count by one wall-clock reading over the whole run. Phase 2 replays the identical operation sequence with two clock reads per operation into an HdrHistogram; that instrumentation cost 11% of the rate in the first run and 14% in the second (`results/throughput.txt`, `results/throughput_run2.txt`), which is why the two are reported apart. Phase 1 varied by 12% between the two runs on an otherwise idle machine, so treat the throughput figure as a magnitude rather than a rank.

`BM_MultiLevelSweep` carries a roughly 12 µs constant offset from Google Benchmark's per-iteration timer pause, which excludes book setup from the timed region. Only the slope across level counts is meaningful, hence the marginal figure above.

## Quickstart

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

./build/tests                             # 58 unit tests
./build/print_sizes                       # struct and wire sizes
./build/bench_throughput 5000000          # sustained throughput + latency percentiles
./build/benchmarks                        # per-operation microbenchmarks
./build/run_replay data/sample_tape.csv   # tape replay with risk and latency report
./build/run_simulation                    # agent simulation
./build/exchange 9876                     # TCP exchange server
```

With the server running, `python3 tools/risk_gate_smoke.py 9876` sends three orders and asserts that the two breaching `RiskLimits` come back rejected with order ID 0, having never reached the book.

CMake fetches GoogleTest, Google Benchmark, and HdrHistogram_c at configure time, so the first configure needs network access.

## Limitations

- `Price` is a `double`. Production engines use scaled integers, because binary floating point cannot represent decimal tick sizes exactly and repeated arithmetic drifts. The `Price` alias exists so this is a one-line change.
- A single mutex serialises the whole engine, so the thread-per-connection server does not scale with connection count. The standard fix is a lock-free queue feeding a single-threaded matching loop.
- The wire protocol writes native byte order and native `double` representation, so a client and server on different architectures would disagree. It is exercised only same-host.
- The maximum latency (277 µs and 248 µs in the two runs) is roughly 3,000x the median. The tail comes from allocator growth, page faults, and OS scheduling; nothing here is preallocated against a tail-latency target, and there is no huge-page or thread-pinning work.
- A p50 of 83 ns is near the granularity of `steady_clock` on this host, which is why the histogram minimum reads 0 ns. Percentiles below roughly 100 ns should be read as approximate.
- Only the simulation's price walk is seeded. Each agent seeds its RNG from `std::random_device`, so trade counts vary run to run (13,760 and 13,919 across two runs here). The replay harness, not the simulation, is the deterministic path.
- The matching engine validates only quantity and price. Self-trade prevention, price bands, and lot sizes are not implemented.
- Matching walks one price level per iteration, so a sweep is linear in levels crossed. No array-indexed-by-tick book.

## Layout

```
include/core/        Types, Order, OrderBook, MatchingEngine, ObjectPool
include/network/     TcpServer, packed binary Protocol
include/risk/        RiskChecker (kill switch, order and position and exposure caps)
include/metrics/     LatencyHistogram (HdrHistogram_c wrapper)
include/replay/      ReplayHarness (CSV tape replay)
include/simulation/  Trader, MarketMaker, MomentumTrader, NoiseTrader, Simulation
src/                 Implementations, mirrors include/
tests/               58 GoogleTest cases across 8 suites
benchmarks/          Per-operation microbenchmarks; sustained-throughput harness
tools/               print_sizes, risk_gate_smoke.py
results/             Committed output of every number quoted above, two throughput runs
data/                51-event sample tape
```

## License

MIT. See [LICENSE](LICENSE).
