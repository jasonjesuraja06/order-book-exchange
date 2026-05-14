# Limit Order Book Exchange

> A high-performance exchange and matching engine in **C++17** with price-time priority order matching, a multi-threaded TCP server, and a full trading simulation. **3.6M orders/sec end-to-end at 262 ns average match latency** on a single core.

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)
![Tests](https://img.shields.io/badge/tests-53%2F53%20passing-success.svg)
![Benchmarks](https://img.shields.io/badge/benchmarks-Google%20Benchmark-orange.svg)
![Latency](https://img.shields.io/badge/p99%20latency-HdrHistogram-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

## Performance

Benchmarked on Apple M3 Pro using Google Benchmark.

| Metric | Value |
|---|---|
| **End-to-end simulation throughput** | **3.6M orders/sec** |
| **Average match latency** | **262 ns** |
| **Minimum match latency** | 41 ns |
| Order insertion throughput | 8.7M orders/sec |
| Order matching throughput | 5.8M orders/sec |
| Order cancellation throughput | 14.9M orders/sec |
| Simulation trades generated | 14,137 |
| Simulation notional volume | $132M |
| Unit tests | 53/53 passing |

## Highlights

- **Price-time (FIFO) priority matching** for limit, market, and IOC orders — the algorithm used by NYSE, NASDAQ, and CME
- **O(1) cancellation** via a hash-indexed iterator map into per-price-level FIFO queues
- **Pre-allocated object pool** eliminating heap allocation on the critical matching path, reducing latency variance
- **Pre-trade risk checks** — kill switch, max order qty / notional, per-symbol position cap, firm-wide notional exposure (the kind of safeguard whose absence cost Knight Capital $440M in 2012)
- **HdrHistogram latency tracking** — full p50 / p90 / p99 / p99.9 distribution with CSV export, the same library used by LMAX Disruptor, Cassandra, and Aeron
- **Deterministic tape-replay harness** — feed historical CSV tapes through the engine for regression testing, exactly how production exchanges certify code changes
- **Multi-threaded TCP server** with a packed binary protocol (23-byte new-order messages, 17-byte cancels)
- **Full trading simulation** with three agent types: inventory-aware market maker, SMA-crossover momentum trader, and noise traders, driven by geometric Brownian motion
- **5 Google Benchmark scenarios** (insert, match, market-fill, cancel, multi-level sweep) + **53 Google Test cases** covering core matching, risk module, latency histogram, and replay harness edge cases

## Architecture

```
        ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
        │ Market Maker │  │   Momentum   │  │    Noise     │
        │     Bot      │  │   Trader     │  │   Traders    │
        └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
               │                 │                 │
               └─────────────────┼─────────────────┘
                                 │ TCP (Binary Protocol)
                        ┌────────▼────────┐
                        │   TCP Server    │
                        │  (Multi-thread) │
                        └────────┬────────┘
                                 │
                        ┌────────▼────────┐
                        │    Matching     │
                        │     Engine      │
                        │  (Price-Time    │
                        │   Priority)     │
                        └────────┬────────┘
                                 │
                 ┌───────────────┼───────────────┐
                 │                               │
          ┌──────▼──────┐                 ┌──────▼──────┐
          │  Bid Book   │                 │  Ask Book   │
          │  (Buys)     │                 │  (Sells)    │
          │ High → Low  │                 │ Low → High  │
          └─────────────┘                 └─────────────┘
```

## Getting Started

### Prerequisites
- C++17-compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.20+

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Run

```bash
./run_simulation                       # full trading simulation (10,000 ticks)
./run_replay data/sample_tape.csv      # tape replay with risk + latency report
./tests                                # 53 unit tests
./benchmarks                           # Google Benchmark suite
./exchange [port]                      # start TCP exchange server (default port 9876)
```

Expected simulation output:

```
╔══════════════════════════════════════════════╗
║          SIMULATION RESULTS                  ║
║  Total Trades:         14,137                ║
║  Total Volume:        813,020 shares         ║
║  Average Latency:         262 ns             ║
║  Throughput:         ~3.6M orders/sec        ║
╚══════════════════════════════════════════════╝
```

## Technical Details

### Matching Algorithm — Price-Time Priority (FIFO)

1. **Price priority** — incoming buys match the lowest-priced resting sells first; incoming sells match the highest-priced resting buys first.
2. **Time priority** — within a price level, earlier orders fill first.
3. **Price improvement** — trades execute at the resting order's price (a buy at $101 matching a sell at $100 trades at $100).

### Order Book Data Structures

| Structure | Implementation | Complexity |
|---|---|---|
| Price levels (bids / asks) | `std::map` with reversed comparator for bids | O(log N) insert, O(1) access to best price |
| Time priority at each level | `std::list<Order*>` FIFO queue | O(1) append, O(1) iterator-based remove |
| Order ID lookup | `std::unordered_map<OrderId, OrderLocation>` storing `{side, price, iterator}` | O(1) cancel and modify |
| Order memory | `ObjectPool<Order>` pre-allocated at startup | No heap allocation on critical path |

### Wire Protocol

Three message types, all fixed-size and packed (no padding):

| Message | Size | Direction |
|---|---|---|
| `OrderMessage` (NewOrder) | **23 bytes** | Client → Exchange |
| `CancelMessage` | 17 bytes | Client → Exchange |
| `ExecutionReport` | 26 bytes | Exchange → Client |

Each starts with a 1-byte `MessageType` enum; fields are laid out with `#pragma pack(push, 1)` so the socket layer can `read()` / `write()` the struct directly without parsing. This is the same idea behind production binary protocols like NASDAQ ITCH and SBE (Simple Binary Encoding).

### Trading Simulation

A tick-based simulation drives a synthetic price via geometric Brownian motion. Each tick, every agent observes the book and submits orders:

- **Market Maker** — quotes both sides with inventory-aware spread skewing; pulls quotes when inventory exceeds a threshold
- **Momentum Trader** — SMA crossover (configurable lookback) with cooldown between trades
- **Noise Traders** — random order flow simulating retail participation

Output includes per-trade records and aggregate statistics (volume, P&L, latency distribution).

### Pre-Trade Risk Checks

Every incoming order passes through a `RiskChecker` before the matching engine sees it. Production exchanges and every trading desk have an equivalent layer — its absence is what allowed Knight Capital to lose $440M in 45 minutes (2012) when a deployment bug bypassed risk controls.

Six checks run in evaluation order:

| # | Check | Reject reason |
|---|---|---|
| 1 | Firm-wide kill switch | `KillSwitchActive` |
| 2 | Quantity > 0 | `InvalidQuantity` |
| 3 | Quantity ≤ `max_order_quantity` | `MaxOrderQuantityExceeded` |
| 4 | Limit order has positive price | `InvalidPrice` |
| 5 | `price × qty` ≤ `max_order_notional` | `MaxOrderNotionalExceeded` |
| 6 | `|net position after fill|` ≤ `max_position_abs` | `MaxPositionExceeded` |
| 7 | Firm-wide notional ≤ `max_notional_exposure` | `MaxNotionalExposureExceeded` |

`RiskChecker.on_fill()` updates per-symbol net positions and the running firm-wide notional after every match. Rejection counts are tracked per reason via `RiskStats`.

### Latency Histograms (HdrHistogram)

`metrics::LatencyHistogram` wraps [HdrHistogram_c](https://github.com/HdrHistogram/HdrHistogram_c) — the standard data structure for recording latency distributions at scale. Key properties:

- **O(1) per record** regardless of sample count
- **Constant memory** (~few KB) across billions of samples
- **Configurable resolution** (default: 3 significant figures = 0.1% bucket accuracy)
- **Coordinated-omission correction** via `record_corrected()` to account for measurement-induced skew

Query any percentile: `histogram.value_at_percentile(99.9)`. Used in production by LMAX Disruptor, Cassandra, Kafka, Elasticsearch, and most major HFT firms. The replay harness (below) populates a per-op histogram and reports p50 / p90 / p99 / p99.9 / max in its summary.

### Tape Replay (Regression Testing)

`replay::ReplayHarness` reads a CSV tape of historical orders + cancels and feeds them through the matching engine. This is the technique every production exchange uses to certify code changes: re-run a representative trading day's events through the new build, diff the trade output against the previous build, and gate the release on zero divergence.

CSV format:

```
timestamp_ns,symbol,side,type,price,quantity,action,order_id
1000000000,AAPL,B,L,150.00,100,N,1
1000001500,AAPL,S,L,150.05,80,N,2
1000005000,AAPL,,,,,C,2
```

`action`: `N` = new order, `C` = cancel. `side`: `B` / `S`. `type`: `L` / `M` / `I` (limit / market / IOC).

A sample 51-event tape is included at `data/sample_tape.csv`. Run with:

```bash
./run_replay data/sample_tape.csv
```

### Memory Layout

The `Order` struct is sized to fit two orders per 64-byte cache line, achieved by ordering fields to minimize alignment padding and using `uint8_t` enums:

```cpp
struct Order {
    OrderId     id;              // 8 bytes
    Price       price;           // 8 bytes
    Quantity    quantity;        // 4 bytes
    Quantity    remaining_qty;   // 4 bytes
    Timestamp   timestamp;       // 8 bytes
    Side        side;            // 1 byte
    OrderType   type;            // 1 byte
    OrderStatus status;          // 1 byte
};  // ~42 bytes total
```

## Project Structure

```
order-book-exchange/
├── include/
│   ├── core/                    # Types, Order, OrderBook, MatchingEngine, ObjectPool
│   ├── network/                 # TcpServer, packed binary Protocol
│   ├── simulation/              # Trader, MarketMaker, MomentumTrader, NoiseTrader, Simulation
│   ├── risk/
│   │   └── RiskChecker.h        # Pre-trade kill switch, position/notional caps
│   ├── metrics/
│   │   └── LatencyHistogram.h   # HdrHistogram wrapper (p50/p99/p99.9)
│   └── replay/
│       └── ReplayHarness.h      # CSV tape replay with risk + latency
├── src/                         # Implementation files (mirrors include/)
├── tests/                       # 53 Google Test cases
│   ├── test_order.cpp           #   4 cases
│   ├── test_orderbook.cpp       #  14 cases
│   ├── test_matching_engine.cpp #  14 cases
│   ├── test_risk_checker.cpp    #  11 cases (limits, kill switch, position tracking)
│   ├── test_latency_histogram.cpp # 6 cases (percentiles, dynamic range)
│   └── test_replay_harness.cpp  #   4 cases (tape replay end-to-end)
├── benchmarks/
│   └── bench_matching_engine.cpp   # 5 Google Benchmark scenarios
├── data/
│   └── sample_tape.csv          # 51-event AAPL/MSFT tape for replay
└── CMakeLists.txt
```

## License

MIT
