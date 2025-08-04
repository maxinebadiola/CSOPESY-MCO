// memory.cpp
#include "headers.h"
#include <filesystem>

vector<MemoryBlock> g_memory_blocks;
mutex g_memory_mutex;


void initializeMemory() {
    lock_guard<mutex> lock(g_memory_mutex);
    g_memory_blocks.clear();
    g_memory_blocks.push_back({
        0, 
        g_max_overall_mem, 
        true, 
        ""
    });
}

int calculatePagesRequired(int memorySize) {
    // P = M / mem-per-frame (rounded up)
    return (memorySize + g_mem_per_frame - 1) / g_mem_per_frame;
}

void printMemoryState(const char* context) {
    lock_guard<mutex> lock(g_memory_mutex);
    cerr << "\nMemory State (" << context << "):\n";
    for (const auto& block : g_memory_blocks) {
        int end_addr = block.start_address + block.size - 1;
        cerr << "[" << block.start_address << "-" << end_addr << "] "
             << (block.is_free ? "FREE" : "USED by " + block.process_name) 
             << "\n";
    }
}

void verifyMemoryConsistency() {
    int total_size = 0;
    for (const auto& block : g_memory_blocks) {
        total_size += block.size;
        if (block.start_address < 0 || block.size <= 0) {
            cerr << "MEMORY CORRUPTION DETECTED!" << endl;
            printMemoryState("CORRUPTION");
            exit(1);
        }
    }
    if (total_size != g_max_overall_mem) {
        cerr << "MEMORY LEAK DETECTED! Expected: " 
             << g_max_overall_mem << " Actual: " << total_size << endl;
        exit(1);
    }
}

bool allocateMemoryFirstFit(PCB* process) {
    lock_guard<mutex> lock(g_memory_mutex);

    int required_size = process->memory_requirement > 0 ? 
                       process->memory_requirement : 
                       g_min_mem_per_proc;

    // Check if already allocated
    for (const auto& block : g_memory_blocks) {
        if (!block.is_free && block.process_name == process->name) {
            return false;
        }
    }

    // Search from the beginning (low addresses) for first fit
    for (auto it = g_memory_blocks.begin(); it != g_memory_blocks.end(); ++it) {
        if (it->is_free && it->size >= required_size) {
            // Allocate at the start of this block
            it->is_free = false;
            it->process_name = process->name;
            
            // If we have remaining space, split the block
            if (it->size > required_size) {
                MemoryBlock new_block = {
                    it->start_address + required_size,
                    it->size - required_size,
                    true,
                    ""
                };
                it->size = required_size;
                g_memory_blocks.insert(next(it), new_block);
            }
            
            verifyMemoryConsistency();
            return true;
        }
    }
    return false;
}

void deallocateMemory(PCB* process) {
    lock_guard<mutex> lock(g_memory_mutex);

    for (auto it = g_memory_blocks.begin(); it != g_memory_blocks.end(); ) {
        if (!it->is_free && it->process_name == process->name) {
            it->is_free = true;
            it->process_name.clear();

            if (it != g_memory_blocks.begin()) {
                auto prev_it = prev(it);
                if (prev_it->is_free) {
                    prev_it->size += it->size;
                    it = g_memory_blocks.erase(it);
                    continue;
                }
            }

            if (next(it) != g_memory_blocks.end()) {
                auto next_it = next(it);
                if (next_it->is_free) {
                    it->size += next_it->size;
                    g_memory_blocks.erase(next_it);
                    continue;
                }
            }
            ++it;
        } else {
            ++it;
        }
    }
    verifyMemoryConsistency();
}