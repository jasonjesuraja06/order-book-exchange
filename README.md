# Limit Order Book Exchange

A high-performance exchange and matching engine built from scratch in C++17, featuring price-time priority order matching, a multi-threaded TCP server with a packed binary protocol, and a full trading simulation with market maker, momentum, and noise trader agents.

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

## Features

- **Matching Engine**: Price-time priority (FIFO) matching for limit, market, and IOC (Immediate-or-Cancel) order types with O(log N) insertion and O(1) cancellation
- **Order Book**: Dual-sided book using sorted maps for price levels and linked lists for time-priority queues within each level, with a hash map index for constant-time order lookup
- **Object Pool**: Pre-allocated memory pool eliminating heap allocations on the critical path, reducing per-order memory overhead
- **TCP Server**: Multi-threaded server accepting concurrent client connections via a packed binary protocol (23 bytes per order message)
- **Trading Simulation**: Three agent types interacting with the order book in a tick-based simulation with geometric Brownian motion price evolution
  - **Market Maker**: Quotes both sides of the book with inventory-aware spread skewing
  - **Momentum Trader**: SMA crossover strategy with configurable lookback window and cooldown
  - **Noise Traders**: Randomized order flow simulating retail participation
- **Benchmarking**: Google Benchmark suite measuring insertion, matching, cancellation, and multi-level sweep performance
- **Testing**: 32 Google Test cases covering order construction, book operations, matching logic, and edge cases

## Performance

Benchmarked on Apple Silicon (M3 Pro):

| Metric | Value |
|--------|-------|
| Order insertion throughput | 8.7M orders/sec |
| Order matching throughput | 5.8M orders/sec |
| Order cancellation throughput | 14.9M orders/sec |
| End-to-end simulation throughput | 3.6M orders/sec |
| Average match latency | 262 nanoseconds |
| Minimum match latency | 41 nanoseconds |
| Simulation trades generated | 14,137 |
| Simulation notional volume | $132M |
| Test cases | 32/32 passing |

## Getting Started

### Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.20 or later

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Run the Simulation

```bash
./run_simulation
```

Expected output:

```
Starting simulation: AAPL @ $150.00 for 10000 ticks
------------------------------------------------------------
   10% complete | Price: $155.01 | Trades: 1331 | Orders: 3074
   ...
  100% complete
------------------------------------------------------------
╔══════════════════════════════════════════════╗
║          SIMULATION RESULTS                 ║
║  Total Trades:         14,137               ║
║  Total Volume:        813,020 shares        ║
║  Average Latency:         262 ns            ║
║  Throughput:         ~3.6M orders/sec       ║
╚══════════════════════════════════════════════╝
```

### Run Tests

```bash
./tests
```

### Run Benchmarks

```bash
./benchmarks
```

### Start the Exchange Server

```bash
./exchange        # Default port 9876
./exchange 5555   # Custom port
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
│   │   └── Protocol.h           # Packed binary message protocol
│   └── simulation/
│       ├── Trader.h             # Base class for trading agents
│       ├── MarketMaker.h        # Two-sided quoting with inventory skew
│       ├── MomentumTrader.h     # SMA crossover strategy
│       ├── NoiseTrader.h        # Random order flow generator
│       └── Simulation.h         # Tick-based simulation orchestrator
├── src/
│   ├── core/                    # Implementation files
│   ├── network/
│   ├── simulation/
│   ├── main.cpp                 # Exchange server entry point
│   └── run_simulation.cpp       # Simulation runner entry point
├── tests/
│   ├── test_order.cpp           # Order struct tests
│   ├── test_orderbook.cpp       # Order book operation tests
│   └── test_matching_engine.cpp # Matching logic tests
├── benchmarks/
│   └── bench_matching_engine.cpp # Google Benchmark suite
└── CMakeLists.txt               # Build configuration
```

## Technical Details

### Matching Algorithm

The engine implements price-time priority (FIFO), the algorithm used by NYSE, NASDAQ, and CME:

1. **Price Priority**: Incoming buy orders match against the lowest-priced sell orders first. Incoming sell orders match against the highest-priced buy orders first.
2. **Time Priority**: At the same price level, orders that arrived earlier are matched first.
3. **Price Improvement**: The trade executes at the resting order's price. A buy order at $101 matching a resting sell at $100 trades at $100.

### Order Book Data Structure

- **Price levels**: `std::map` with custom comparators (descending for bids, ascending for asks) providing O(log N) insertion and O(1) access to the best price
- **Time priority**: `std::list` (doubly-linked list) at each price level, providing O(1) append for new orders and O(1) removal for fills and cancels
- **Order lookup**: `std::unordered_map` from order ID to location (side, price, iterator), enabling O(1) cancellation regardless of book depth

### Object Pool

Orders are allocated from a pre-sized pool at startup. When an order is filled or cancelled, its memory is returned to the pool rather than freed. This eliminates per-order heap allocation on the critical matching path, reducing latency variance.

### Trading Simulation

The simulation uses a tick-based architecture with geometric Brownian motion for price evolution. Each tick, the "true" price random-walks and all agents observe the market and submit orders. The market maker provides liquidity by quoting both sides with inventory-aware spread skewing. The momentum trader uses a simple moving average crossover to detect trends. Noise traders generate random order flow to simulate retail participation.

## License

MIT
