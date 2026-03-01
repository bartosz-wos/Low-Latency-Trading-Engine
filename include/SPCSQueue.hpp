#pragma once
#include <atomic>
#include <cstddef>

template<typename T, size_t Size>
class SPSCQueue{
	static_assert((Size&(Size-1))==0, "The buffor size should be a power of 2!");
private:
	T buffer[Size];
	alignas(64) std::atomic<size_t> head{0};
	alignas(64) std::atomic<size_t> tail{0};
public:
	bool push(const T&item){
		size_t cur_head = head.load(std::memory_order_relaxed);
		if(cur_head - tail.load(std::memory_order_acquire) == Size){
			return false;
		}
		buffer[cur_head & (Size - 1)] = item;
		head.store(cur_head + 1, std::memory_order_release);
		return true;
	}
	bool pop(T&item){
		size_t cur_tail = tail.load(std::memory_order_relaxed);
		if(cur_tail == head.load(std::memory_order_acquire)){
			return false;
		}
		item = buffer[cur_tail & (Size - 1)];
		tail.store(cur_tail + 1, std::memory_order_release);
		return true;
	}
};
