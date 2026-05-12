# Limit Order Book Exchange

> A high-performance exchange and matching engine in **C++17** with price-time priority order matching, a multi-threaded TCP server, and a full trading simulation. **3.6M orders/sec end-to-end at 262 ns average match latency** on a single core.

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-green.svg)
![Tests](https://img.shields.io/badge/tests-32%2F32%20passing-success.svg)
![Benchmarks](https://img.shields.io/badge/benchmarks-Google%20Benchmark-orange.svg)
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
| Unit tests | 32/32 passing |

## Highlights

- **Price-time (FIFO) priority matching** for limit, market, and IOC orders — the algorithm used by NYSE, NASDAQ, and CME
- **O(1) cancellation** via a hash-indexed iterator map into per-price-level FIFO queues
- **Pre-allocated object pool** eliminating heap allocation on the critical matching path, reducing latency variance
- **Multi-threaded TCP server** with a packed binary protocol (23-byte new-order messages, 17-byte cancels)
- **Full trading simulation** with three agent types: inventory-aware market maker, SMA-crossover momentum trader, and noise traders, driven by geometric Brownian motion
- **5 Google Benchmark scenarios** (insert, match, market-fill, cancel, multi-level sweep) + **32 Google Test cases** covering edge cases

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
./run_simulation     # full trading simulation (10,000 ticks)
./tests              # 32 unit tests
./benchmarks         # Google Benchmark suite
./exchange [port]    # start TCP exchange server (default port 9876)
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
│   ├── core/
│   │   ├── Types.h              # Type aliases, enums, timestamps
│   │   ├── Order.h              # Order and Trade structs
│   │   ├── OrderBook.h          # Dual-sided order book
│   │   ├── MatchingEngine.h     # Price-time priority matching
│   │   └── ObjectPool.h         # Pre-allocated memory pool
│   ├── network/
│   │   ├── TcpServer.h          # Multi-threaded TCP server
│   │   └── Protocol.h           # Packed binary protocol
│   └── simulation/
│       ├── Trader.h             # Base class for trading agents
│       ├── MarketMaker.h        # Two-sided quoting with inventory skew
│       ├── MomentumTrader.h     # SMA crossover strategy
│       ├── NoiseTrader.h        # Random order flow
│       └── Simulation.h         # Tick-based orchestrator
├── src/                         # Implementation files
├── tests/                       # 32 Google Test cases
│   ├── test_order.cpp           #   4 cases
│   ├── test_orderbook.cpp       #  14 cases
│   └── test_matching_engine.cpp #  14 cases
├── benchmarks/
│   └── bench_matching_engine.cpp   # 5 Google Benchmark scenarios
└── CMakeLists.txt
```

## License

MIT
