#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <memory>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <x86intrin.h>
#include "../include/LOB.hpp"
#include "../include/SPSCQueue.hpp"

struct MarketSnapshot {
    uint64_t bid_prices[5];
    uint64_t bid_sizes[5];
    uint64_t ask_prices[5];
    uint64_t ask_sizes[5];
};

struct OrderMsg {
    uint64_t id;
    uint64_t price;
    uint64_t size;
    Side side;
};

double get_ns(){
	auto start_time = std::chrono::high_resolution_clock::now();
    	uint64_t start_tsc = __rdtsc();
	while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count() < 10){
        	std::this_thread::yield();
    	}
	uint64_t end_tsc = __rdtsc();
    	auto end_time = std::chrono::high_resolution_clock::now();
    	uint64_t elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    	uint64_t elapsed_tsc = end_tsc - start_tsc;
	return static_cast<double>(elapsed_ns) / elapsed_tsc;
}

SPSCQueue<OrderMsg, 2097152> order_queue;

std::vector<MarketSnapshot> historical_data;

void load_csv(const std::string& filename) {
    std::cout << "loading " << filename << std::endl;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "can't open file" << std::endl;
        exit(1);
    }

    std::string line;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string segment;
        MarketSnapshot snap;

        std::getline(ss, segment, ',');

        auto parse_val = [&](const std::string& s, bool is_price) -> uint64_t {
            try {
                double val = std::stod(s);
                if (is_price) {
                    uint64_t scaled = static_cast<uint64_t>(val * 100.0); 
                    return scaled % 99999 + 1; 
                } else {
                    return static_cast<uint64_t>(val * 10000.0);
                }
            } catch (...) { return 0; }
        };

        for (int i = 0; i < 5; i++) {
            std::getline(ss, segment, ','); snap.bid_prices[i] = parse_val(segment, true);
            std::getline(ss, segment, ','); snap.bid_sizes[i] = parse_val(segment, false);
        }

        for (int i = 0; i < 5; i++) {
            std::getline(ss, segment, ','); snap.ask_prices[i] = parse_val(segment, true);
            std::getline(ss, segment, ','); snap.ask_sizes[i] = parse_val(segment, false);
        }

        historical_data.push_back(snap);
    }
    std::cout << "[Loader] Loaded " << historical_data.size() << " snapshots." << std::endl;
}

void market_thread_func() {
    for (volatile int i = 0; i < 10000; ++i);
    uint64_t order_id = 1;
    
    for (const auto& snap : historical_data) {
        
        for (int i = 0; i < 5; i++) {
            OrderMsg msg = {order_id++, snap.bid_prices[i], snap.bid_sizes[i], Side::BID};
            while (!order_queue.push(msg)) { std::this_thread::yield(); }
        }

        for (int i = 0; i < 5; i++) {
            OrderMsg msg = {order_id++, snap.ask_prices[i], snap.ask_sizes[i], Side::ASK};
            while (!order_queue.push(msg)) { std::this_thread::yield(); }
        }
    }

    while (!order_queue.push({0, 0, 0, Side::BID}));
    std::cout << "all orders sent" << std::endl;
}

void engine_thread_func() {
    auto lob = std::make_unique<LimitOrderBook>();
    OrderMsg msg;
    uint64_t count = 0;

    std::vector<uint64_t> latencies;
    latencies.reserve(10000000); 

    double tsc_to_ns = get_ns();

    std::cout << "ready + waiting for data" << std::endl;
    while (!order_queue.pop(msg)) {}
    auto global_start = std::chrono::high_resolution_clock::now();

    if (msg.id != 0) {
        uint64_t t1 = __rdtsc();
        lob->add_order(msg.id, msg.price, msg.size, msg.side);
        uint64_t t2 = __rdtsc();
        latencies.push_back(t2 - t1);
        count++;
    }

    while (true) {
        while (!order_queue.pop(msg)){}
        if (msg.id == 0 && msg.size == 0) break;
        uint64_t t1 = __rdtsc();
        lob->add_order(msg.id, msg.price, msg.size, msg.side);
        uint64_t t2 = __rdtsc();
        latencies.push_back(t2 - t1);
        count++;
    }

    auto global_end = std::chrono::high_resolution_clock::now();
    auto global_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(global_end - global_start).count();

    for(auto&x : latencies){
	    x = static_cast<uint64_t>(x * tsc_to_ns);
    }

    std::sort(latencies.begin(), latencies.end());
    
    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    double mean = sum / count;

    double var = 0;
    for (auto lat : latencies) {
        var += (lat - mean) * (lat - mean);
    }
    var /= count;
    double std = std::sqrt(var);

    uint64_t min_lat = latencies.front();
    uint64_t max_lat = latencies.back();
    uint64_t p50 = latencies[count * 0.50];
    uint64_t p90 = latencies[count * 0.90];
    uint64_t p99 = latencies[count * 0.99];
    uint64_t p99_9 = latencies[count * 0.999];
    uint64_t p99_99 = latencies[count * 0.9999];

    std::cout << "processed " << count << " orders" << std::endl;
    std::cout << "total time " << global_duration / 1e6 << " ms" << std::endl;
    std::cout << "min       " << min_lat << " ns" << std::endl;
    std::cout << "mean      " << std::fixed << std::setprecision(2) << mean << " ns" << std::endl;
    std::cout << "standard deviation   " << std << " ns" << std::endl;
    std::cout << "50th percentile: " << p50 << " ns" << std::endl;
    std::cout << "90th percentile       " << p90 << " ns" << std::endl;
    std::cout << "99th percentile      " << p99 << " ns" << std::endl;
    std::cout << "99.9th percentile     " << p99_9 << " ns" << std::endl;
    std::cout << "99.99 percentile   " << p99_99 << " ns" << std::endl;
    std::cout << "max time       " << max_lat << " ns" << std::endl;

    lob->debug();
}

int main() {
    load_csv("ml/binance_top5_data.csv");
    if (historical_data.empty()) {
        std::cerr << "no data loaded" << std::endl;
        return 1;
    }

    std::thread engine(engine_thread_func);
    std::thread market(market_thread_func);

    market.join();
    engine.join();

    return 0;
}
