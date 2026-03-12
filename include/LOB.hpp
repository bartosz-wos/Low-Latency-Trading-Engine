#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

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

template<size_t PoolSize = 2000000>
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

template<size_t MaxPrice=100000>
class PriceBitset{
private:
	static constexpr size_t NUM_VARS = (MaxPrice + 63) >> 6;
	
	uint64_t vars[NUM_VARS] = {0};

public:
	inline void set_active(uint64_t price){
		vars[price >> 6] |= (1ULL << (price & 63));
	}

	inline void set_inactive(uint64_t price){
		vars[price >> 6] &= ~(1ULL << (price & 63));
	}

	inline uint64_t find_next_ask(uint64_t price) const{
		size_t word_idx = price >> 6;
		size_t bit_idx = price & 63;
		uint64_t mask = vars[word_idx] & (~0ULL << bit_idx);

		if(mask != 0){
			return (word_idx << 6) + __builtin_ctzll(mask);
		}
		
		for(size_t i = word_idx + 1; i < NUM_VARS; ++i){
			if(vars[i] != 0){
				return (i << 6) + __builtin_ctzll(vars[i]);
			}
		}
		return UINT64_MAX;
	}

	inline uint64_t find_next_bid(uint64_t price) const{
		size_t word_idx = price >> 6;
		size_t bit_idx = price & 63;
		uint64_t mask = vars[word_idx];

		if(bit_idx < 63){
			mask &= (1ULL << (bit_idx + 1)) - 1;
		}

		if(mask != 0){
			return (word_idx << 6) + (63 - __builtin_clzll(mask));
		}

		for(size_t i = word_idx; i-- > 0;){
			if(vars[i] != 0){
				return (i << 6) + (63 - __builtin_clzll(vars[i]));
			}
		}
		return 0;
	}
};

template<size_t Capacity=8388608>
class FibMap{
private:
        static_assert((Capacity & (Capacity - 1)) == 0, "map size must be power of 2");

        static constexpr uint64_t EMPTY = 0;
        static constexpr uint64_t DEL = UINT64_MAX;
	static constexpr uint64_t SHIFT = 64 - __builtin_ctzll(Capacity);

        struct Item{
		uint64_t key;
		Order* val;
        };

	Item* table;

	static inline size_t hash(uint64_t key){
		return (key * 11400714819323198485ULL) >> SHIFT;
	}
public:
	FibMap(){
		table = new Item[Capacity]();
	}

	~FibMap(){
		delete[] table;
	}

	inline void insert(uint64_t key, Order* item){
		size_t idx = hash(key);
		while(table[idx].key != EMPTY && table[idx].key != DEL){
			if(__builtin_expect(table[idx].key == key, 0)){
				table[idx].val = item;
				return;
			}
			idx = (idx + 1) & (Capacity - 1);
		}
		table[idx].key = key;
		table[idx].val = item;
	}

	inline Order* find(uint64_t key) const{
		size_t idx = hash(key);
		while(table[idx].key != EMPTY){
			if(table[idx].key == key){
				return table[idx].val;
			}
			idx = (idx + 1) & (Capacity - 1);
		}
		return nullptr;
	}

	inline void erase(uint64_t key){
		size_t idx = hash(key);
		while(table[idx].key != EMPTY){
			if(table[idx].key == key){
				table[idx].key = DEL;
				return;
			}
			idx = (idx + 1) & (Capacity - 1);
		}
	}
};

template<size_t Capacity=4096>
class FlatMap{
private:
	struct Item{
		uint64_t key;
		PriceLevel* level;
	};

	Item table[Capacity];
	size_t count = 0;

	inline size_t find_pos(uint64_t key) const{
		size_t lef = 0;
		size_t rig = count;
		while(lef < rig){
			size_t mid = (lef + rig) >> 1;
			if(table[mid].key < key){
				lef = mid + 1;
			}else{
				rig = mid;
			}
		}
		return lef;
	}

public:
	inline bool empty(){
		return count == 0;
	}

	inline uint64_t get_min() const{
		return count > 0 ? table[0].key : UINT64_MAX;
	}

	inline uint64_t get_max() const{
		return count > 0 ? table[count - 1].key : 0;
	}

	inline PriceLevel* find(uint64_t key) const{
		size_t pos = find_pos(key);
		if(pos < count && table[pos].key == key){
			return table[pos].level;
		}
		return nullptr;
	}

	inline void insert(uint64_t key, PriceLevel* level){
		size_t pos = find_pos(key);

		if(__builtin_expect(pos < count && table[pos].key == key, 0)){
			table[pos].level = level;
			return;
		}

		if(__builtin_expect(count >= Capacity, 0)){
			throw std::runtime_error("FlatMap full");
		}

		if(pos < count){
			std::memmove(&table[pos + 1], &table[pos], (count - pos) * sizeof(Item));
		}

		table[pos].key = key;
		table[pos].level = level;
		count++;
	}

	inline void erase(uint64_t key){
		size_t pos = find_pos(key);

		if(pos < count && table[pos].key == key){
			if(pos < count - 1){
				std::memmove(&table[pos], &table[pos + 1], (count - pos - 1) * sizeof(Item));
			}
			count--;
		}
	}
};

enum class OrderType : uint8_t{
	LIMIT = 0,
	MARKET = 1
};

class LimitOrderBook{
private:
	static constexpr uint64_t FAST_PRICE_MAX = 100000;

	PriceLevel fast_bids[FAST_PRICE_MAX];
	PriceLevel fast_asks[FAST_PRICE_MAX];

	FlatMap<4096> slow_bids;
	FlatMap<4096> slow_asks;

	FibMap<8388608> order_map;

	OrderPool<2000000> order_pool;
	PriceLevelPool<20000> level_pool;

	PriceBitset<FAST_PRICE_MAX> fast_bids_bitset;
	PriceBitset<FAST_PRICE_MAX> fast_asks_bitset;

	uint64_t best_bid;
	uint64_t best_ask;

	inline PriceLevel* get_or_create_level(uint64_t price, Side side){
		if(price < FAST_PRICE_MAX){
			return (side == Side::BID) ? &fast_bids[price] : &fast_asks[price];
		}else{
			auto& slow_map = (side == Side::BID) ? slow_bids : slow_asks;
			PriceLevel* level = slow_map.find(price);

			if(__builtin_expect(level == nullptr, 0)){
				level = level_pool.allocate(price);
				slow_map.insert(price, level);
			}
			return level;
		}
	}
public:
	LimitOrderBook() : best_bid(0), best_ask(UINT64_MAX){
		for(uint64_t i = 0; i < FAST_PRICE_MAX; i++){
			fast_bids[i].price = i;
			fast_asks[i].price = i;
		}
	}

    	void debug(){
        	if(best_bid == 0) std::cout << "Best Bid: NONE" << std::endl;
        	else std::cout << "Best Bid: " << best_bid << std::endl;

        	if(best_ask == UINT64_MAX) std::cout << "Best Ask: NONE" << std::endl;
        	else std::cout << "Best Ask: " << best_ask << std::endl;
    	}

    	inline uint64_t get_best_bid_volume(){
        	if(best_bid == 0) return 0;
        	if(best_bid < FAST_PRICE_MAX) 
			return fast_bids[best_bid].total_volume;

		PriceLevel* level = slow_bids.find(best_bid);
		return (level != nullptr) ? level->total_volume : 0;
    	}

    	inline uint64_t get_best_ask_volume(){
        	if(best_ask == UINT64_MAX) return 0;
        	if(best_ask < FAST_PRICE_MAX) 
			return fast_asks[best_ask].total_volume;

		PriceLevel* level = slow_asks.find(best_ask);
                return (level != nullptr) ? level->total_volume : 0;
    	}

	inline uint64_t get_best_bid_price(){
		return best_bid;
	}

	inline uint64_t get_best_ask_price(){
		return best_ask;
	}

	inline void add_order(uint64_t order_id, uint64_t price, uint64_t size, Side side, OrderType type = OrderType::LIMIT){
		if(__builtin_expect(type == OrderType::LIMIT, 1)){
			add_limit_order(order_id, price, size, side);
		}else{
			add_market_order(size, side);
		}
	}

	inline void add_limit_order(uint64_t order_id, uint64_t price, uint64_t size, Side side){
		if(side == Side::BID){
			while(size > 0 && best_ask <= price && best_ask != UINT64_MAX){
				PriceLevel* ask_level = get_or_create_level(best_ask, Side::ASK);

				Order* resting_ask = ask_level->head;
				while(resting_ask != nullptr && size > 0){
					uint64_t trade_volume = (size < resting_ask->size) ? size : resting_ask->size;

					size -= trade_volume;
					resting_ask->size -= trade_volume;
					ask_level->total_volume -= trade_volume;

					if(resting_ask->size == 0){
						Order* to_delete = resting_ask;
						resting_ask = resting_ask->next;
						order_map.erase(to_delete->order_id);
						ask_level->remove_order(to_delete);
						order_pool.deallocate(to_delete);
					}else{
						break;
					}
				}

				if(ask_level->total_volume == 0){
					if(best_ask < FAST_PRICE_MAX){
                                                fast_asks_bitset.set_inactive(best_ask);
						best_ask = fast_asks_bitset.find_next_ask(best_ask + 1);
						if(best_ask == UINT64_MAX && !slow_asks.empty()){
							best_ask = slow_asks.get_min();
						}
					}else{
						slow_asks.erase(best_ask);
						level_pool.deallocate(ask_level);
						best_ask = slow_asks.empty() ? UINT64_MAX : slow_asks.get_min();
					}
				}
			}
		}else{
			while(size > 0 && best_bid >= price && best_bid != 0){
				PriceLevel* bid_level = get_or_create_level(best_bid, Side::BID);

				Order* resting_bid = bid_level->head;
				while(resting_bid != nullptr && size > 0){
					uint64_t trade_volume = (size < resting_bid->size) ? size : resting_bid->size;

					size -= trade_volume;
					resting_bid->size -= trade_volume;
					bid_level->total_volume -= trade_volume;

					if(resting_bid->size == 0){
						Order* to_delete = resting_bid;
						resting_bid = resting_bid->next;
						order_map.erase(to_delete->order_id);
						bid_level->remove_order(to_delete);
						order_pool.deallocate(to_delete);
					}else{
						break;
					}
				}

				if(bid_level->total_volume == 0){
					if(best_bid < FAST_PRICE_MAX){
						fast_bids_bitset.set_inactive(best_bid);
						best_bid = fast_bids_bitset.find_next_bid(best_bid - 1);

						if(best_bid == 0 && !slow_bids.empty()){
							best_bid = slow_bids.get_max();
						}
					}else{
						slow_bids.erase(best_bid);
						level_pool.deallocate(bid_level);
						if(slow_bids.empty()){
							best_bid = fast_bids_bitset.find_next_bid(FAST_PRICE_MAX - 1);
						}else{
							best_bid = slow_bids.get_max();
						}
					}
				}
			}
		}

		if(size > 0){
			PriceLevel* level = get_or_create_level(price, side);
			Order* new_order = order_pool.allocate();
			new_order->order_id = order_id;
			new_order->price = price;
			new_order->size = size;
			new_order->side = side;
			level->append_order(new_order);
			order_map.insert(order_id, new_order);

			if(price < FAST_PRICE_MAX){
                                if(side == Side::BID)
                                        fast_bids_bitset.set_active(price);
                                else
                                        fast_asks_bitset.set_active(price);
                        }

			if(side == Side::BID && price > best_bid){
				best_bid = price;
			}else if(side == Side::ASK && price < best_ask){
				best_ask = price;
			}
		}
	}

	inline void add_market_order(uint64_t size, Side side){
		if(side == Side::BID){
			while(size > 0 && best_ask != UINT64_MAX){
				PriceLevel* ask_level = get_or_create_level(best_ask, Side::ASK);

				Order* rest_ask = ask_level->head;
				while(rest_ask != nullptr && size > 0){
					__builtin_prefetch(rest_ask->next, 0, 0);

					uint64_t trade_vol = (size < rest_ask->size) ? size : rest_ask->size;
					size -= trade_vol;
					rest_ask->size -= trade_vol;
					ask_level->total_volume -= trade_vol;

					if(rest_ask->size == 0){
						Order* to_delete = rest_ask;
						rest_ask = rest_ask->next;
						order_map.erase(to_delete->order_id);
						ask_level->remove_order(to_delete);
						order_pool.deallocate(to_delete);
					}else{
						break;
					}
				}

				if(ask_level->total_volume == 0){
					if(best_ask < FAST_PRICE_MAX){
						fast_asks_bitset.set_inactive(best_ask);
						best_ask = fast_asks_bitset.find_next_ask(best_ask + 1);
						if(best_ask == UINT64_MAX && !slow_asks.empty()){
							best_ask = slow_asks.get_min();
						}
					}else{
						slow_asks.erase(best_ask);
						level_pool.deallocate(ask_level);
						best_ask = slow_asks.empty() ? UINT64_MAX : slow_asks.get_min();
					}
				}
			}
		}else{
			while(size > 0 && best_bid != 0){
				PriceLevel* bid_level = get_or_create_level(best_bid, Side::BID);

				Order* rest_bid = bid_level->head;
				while(rest_bid != nullptr && size > 0){
					 __builtin_prefetch(rest_bid->next, 0, 0);

					uint64_t trade_vol = (size < rest_bid->size) ? size : rest_bid->size;

					size -= trade_vol;
					rest_bid->size -= trade_vol;
					bid_level->total_volume -= trade_vol;

					if(rest_bid->size == 0){
						Order* to_delete = rest_bid;
						rest_bid = rest_bid->next;
						order_map.erase(to_delete->order_id);
						bid_level->remove_order(to_delete);
						order_pool.deallocate(to_delete);
					}else{
						break;
					}
				}

				if(bid_level->total_volume == 0){
					if(best_bid < FAST_PRICE_MAX){
						fast_bids_bitset.set_inactive(best_bid);
						best_bid = fast_bids_bitset.find_next_bid(best_bid - 1);

						if(best_bid == 0 && !slow_bids.empty()){
							best_bid = slow_bids.get_max();
						}
					}else{
						slow_bids.erase(best_bid);
						level_pool.deallocate(bid_level);	
						if(slow_bids.empty()){
							best_bid = fast_bids_bitset.find_next_bid(FAST_PRICE_MAX - 1);
						}else{
							best_bid = slow_bids.get_max();
						}
					}
				}
			}
		}
	}

	inline void cancel_order(uint64_t order_id){
		Order* order = order_map.find(order_id);
		if(__builtin_expect(order == nullptr, 0)){
			return;
		}

		uint64_t price = order->price;
		Side side = order->side;

		order_map.erase(order_id);

		PriceLevel* level = get_or_create_level(price, side);
		level->remove_order(order);
		order_pool.deallocate(order);

		if(__builtin_expect(level->total_volume == 0, 0)){
			if(side == Side::BID){
				if(price == best_bid){
					if(best_bid < FAST_PRICE_MAX){
						fast_bids_bitset.set_inactive(best_bid);
						best_bid = fast_bids_bitset.find_next_bid(best_bid - 1);
						
						if(best_bid == 0 && !slow_bids.empty()){
							best_bid = slow_bids.get_max();
						}
					}else{
						slow_bids.erase(best_bid);
						level_pool.deallocate(level);
						if(slow_bids.empty()){
							best_bid = fast_bids_bitset.find_next_bid(FAST_PRICE_MAX - 1);
						}else{
							best_bid = slow_bids.get_max();
						}
					}
				}else if(price < FAST_PRICE_MAX){
					fast_bids_bitset.set_inactive(price);
				}else{
					slow_bids.erase(price);
					level_pool.deallocate(level);
				}
			}else{
				if(price == best_ask){
					if(best_ask < FAST_PRICE_MAX){
						fast_asks_bitset.set_inactive(best_ask);
						best_ask = fast_asks_bitset.find_next_ask(best_ask + 1);
						if(best_ask == UINT64_MAX && !slow_asks.empty()){
							best_ask = slow_asks.get_min();
						}
					}else{
						slow_asks.erase(best_ask);
						level_pool.deallocate(level);
						if(slow_asks.empty()){
							best_ask = UINT64_MAX;
						}else{
							best_ask = slow_asks.get_min();
						}
					}
				}else if(price < FAST_PRICE_MAX){
					fast_asks_bitset.set_inactive(price);
				}else{
					slow_asks.erase(price);
					level_pool.deallocate(level);
				}
			}
		}
	}
};
