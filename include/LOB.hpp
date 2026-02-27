#pragma once

#include <cstdint>
#include <stdexcept>
#include <unordered_map>

enum class Side : uint8_t{
	BID = 1,
	ASK = 0
};

struct Order{
	uint64_t order_id;
	uint64_t price;
	uint64_t size;
	Side side;
	Order* next;
	Order* prev;
};

template<size_t PoolSize = 100000>
class OrderPool{
private:
	Order data[PoolSize];
	Order* free_list[PoolSize];
	size_t free_count;
public:
	OrderPool(){
		for(size_t i = 0; i < PoolSize; ++i){
			free_list[i] = &data[i];
		}
		free_count = PoolSize;
	}
	inline Order* allocate(){
		if(__builtin_expect(free_count == 0, 0)){
			throw std::runtime_error("FATAL: OrderPool full!");
		}
		return free_list[--free_count];
	}
	inline void deallocate(Order* order){
		free_list[free_count++] = order;
	}
};

struct PriceLevel{
	uint64_t price;
	uint64_t total_volume;

	Order* head;
	Order* tail;

	PriceLevel(uint64_t p = 0) : price(p), total_volume(0), head(nullptr), tail(nullptr){}

	inline void append_order(Order* order){
		order->price = price;
		order->next = nullptr;
		order->prev = tail;

		if(tail != nullptr){
			tail->next = order;
		}else{
			head = order;
		}
		tail = order;
		total_volume += order->size;
	}

	inline void remove_order(Order* order){
		if(order->prev != nullptr){
			order->prev->next = order->next;
		}else{
			head = order->next;
		}
		if(order->next != nullptr){
			order->next->prev = order->prev;
		}else{
			tail = order->prev;
		}
		total_volume -= order->size;
	}
};

template<size_t PoolSize = 10000>
class PriceLevelPool{
private:
	PriceLevel data[PoolSize];
	PriceLevel* free_list[PoolSize];
	size_t free_count;
public:
	PriceLevelPool(){
		for(size_t i = 0; i < PoolSize; ++i){
			free_list[i] = &data[i];
		}
		free_count = PoolSize;
	}
	inline PriceLevel* allocate(uint64_t price){
		if(__builtin_expect(free_count == 0, 0)){
			throw std::runtime_error("FATAL: PriceLevelPool full!");
		}
		PriceLevel* level = free_list[--free_count];
		level->price = price;
		level->total_volume = 0;
		level->head = nullptr;
		level->tail = nullptr;
		return level;
	}
	inline void deallocate(PriceLevel* level){
		free_list[free_count++] = level;
	}
};

class LimitOrderBook{
private:
	static constexpr uint64_t FAST_PRICE_MAX = 100000;

	PriceLevel fast_bids[FAST_PRICE_MAX];
	PriceLevel fast_asks[FAST_PRICE_MAX];

	std::unordered_map<uint64_t, PriceLevel*> slow_bids;
	std::unordered_map<uint64_t, PriceLevel*> slow_asks;

	OrderPool<> order_pool;
	PriceLevelPool<> level_pool;

	uint64_t best_bid;
	uint64_t best_ask;

	inline PriceLevel* get_or_create_level(uint64_t price, Side side){
		if(price < FAST_PRICE_MAX){
			return (side == Side::BID) ? &fast_bids[price] : &fast_asks[price];
		}else{
			auto& slow_map = (side == Side::BID) ? slow_bids : slow_asks;
			auto [it, inserted] = slow_map.try_emplace(price, nullptr);
			if(inserted){
				it->second = level_pool.allocate(price);
			}
			return it->second;
		}
	}
public:
	LimitOrderBook() : best_bid(0), best_ask(UINT64_MAX){
		for(uint64_t i = 0; i < FAST_PRICE_MAX; i++){
			fast_bids[i].price = i;
			fast_asks[i].price = i;
		}
	}

	inline void add_order(uint64_t order_id, uint64_t price, uint64_t size, Side side){
		PriceLevel* level = get_or_create_level(price, side);
		Order* new_order = order_pool.allocate();
		new_order->order_id = order_id;
		new_order->price = price;
		new_order->size = size;
		new_order->side = side;

		level->append_order(new_order);

		if(side == Side::BID && price > best_bid){
			best_bid = price;
		}else if(side == Side::ASK && price < best_ask){
			best_ask = price;
		}
	}
};
