#pragma once

#include <atomic>
#include <cstdint>

#define SHM_NAME "/shared_memory"
#define SHM_SIZE 4096

struct SharedState{
	uint64_t check_val;

	alignas(64) struct{
		std::atomic<uint64_t> sequence_id;
		uint64_t timestamp;
		double best_bid_price;
		double best_ask_price;
		double best_bid_vol;
		double best_ask_vol;
	} market_data;

	alignas(64) struct{
		std::atomic<double> target_pos;
		std::atomic<bool> strat_ready;
		std::atomic<bool> python_alive;
	} strat_data;
};
