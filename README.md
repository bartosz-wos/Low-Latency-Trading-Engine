## Low-Latency-Trading-Engine

Limit order book and execution engine built for algorithmic trading.
It has a strict focus on memory layout and zero allocation.

## Architecture

- **Execution Engine in C++:** does order matching, book updates and execution
- **Trading Strategies in Python:** runs a simple trained xgboost model and processes data

The code uses a SPSC queue and shared memory for efficient communcation between C++ and Python programs

## Optimizations

- **no STL:** custom data structures (fibmap and flatmap) to omit heap allocations
- **Pre-allocated memory pools:** OrderPool and PriceLevelPool eliminate dynamic alloc latency during execution
- **Prefetching in add order:** `__builtin_prefetch` is used to pull cache lines, so it reduces RAM latency
- **Optimization with __builtin_expect**
- **Hardware TSC for measuring time:** __rdtsc() bypasses OS overhead

## Benchmark

Performance was tested on a single core using more than 9 million Binance order snapshots of L1...L5.

- **Median latency:** `10 ns`
- **Mean latency:** `33 ns`
- **99.99th percentile latency:** `8 us`
- **Total time for about 9.4 million orders:** `573 ms`

## Build and Run

```bash
g++ -O3 -march=native src/main.cpp -o matching_engine
taskset -c 2 ./matching_engine
```
