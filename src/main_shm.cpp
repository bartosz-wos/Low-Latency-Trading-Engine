#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cmath>
#include <iomanip>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../include/SharedMem.hpp" 

struct MarketSnapshot {
    double bid_prices[5];
    double bid_vols[5];
    double ask_prices[5];
    double ask_vols[5];
};

std::vector<MarketSnapshot> historical_data;

void load_csv(const std::string& filename) {
    std::ifstream file(filename);
    if(!file.is_open()){
        std::cerr << "cannot open file " << filename << std::endl;
        exit(1);
    }
    
    std::string line;
    int row_count = 0;

    while(std::getline(file, line)){
        if(line.empty()) continue;

        std::stringstream ss(line);
        std::string segment;
        MarketSnapshot snap;

        std::getline(ss, segment, ','); 

        try {
            for(int i = 0; i < 5; ++i){
                std::getline(ss, segment, ','); snap.bid_prices[i] = std::stod(segment);
                std::getline(ss, segment, ','); snap.bid_vols[i]   = std::stod(segment);
            }
            for(int i = 0; i < 5; ++i){
                std::getline(ss, segment, ','); snap.ask_prices[i] = std::stod(segment);
                std::getline(ss, segment, ','); snap.ask_vols[i]   = std::stod(segment);
            }
            historical_data.push_back(snap);
            row_count++;
        } catch (...) {
            continue;
        }
    }
    std::cout << "loaded " << row_count << " snapshots" << std::endl;
}

int main() {
    load_csv("ml/binance_top5_data.csv");
    
    if(historical_data.empty()) {
        std::cerr << "no data loaded" << std::endl;
        return 1;
    }

    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) { perror("shm_open"); return 1; }
    
    ftruncate(fd, SHM_SIZE);
    void* ptr = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) { perror("mmap"); return 1; }
    
    SharedState* shm = new (ptr) SharedState();
    
    shm->market_data.sequence_id = 0;
    shm->strat_data.target_pos = 0.0;

    std::cout << "memory mapped, waiting for python..." << std::endl;

    volatile unsigned char* raw_mem = (volatile unsigned char*)ptr;
    while(raw_mem[137] != 1){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "connected and starting" << std::endl;

    uint64_t seq = 0;

    for(const auto& snap : historical_data){
        shm->market_data.best_bid_price = snap.bid_prices[0];
        shm->market_data.best_ask_price = snap.ask_prices[0];
        shm->market_data.best_bid_vol   = snap.bid_vols[0];
        shm->market_data.best_ask_vol   = snap.ask_vols[0];
        
        shm->market_data.sequence_id.store(++seq, std::memory_order_release);

        double signal = shm->strat_data.target_pos.load(std::memory_order_acquire);
        
        std::this_thread::sleep_for(std::chrono::microseconds(500)); 
    }
    
    munmap(ptr, SHM_SIZE);
    close(fd);
    shm_unlink(SHM_NAME);
    return 0;
}
