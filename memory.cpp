// memory.cpp
#include "headers.h"
#include <filesystem>
#include <sstream>
#include <iomanip>

vector<MemoryBlock> g_memory_blocks;
mutex g_memory_mutex;

// Memory space for process memory access
vector<uint16_t> g_memory_space;
mutex g_memory_space_mutex;
int g_memory_space_size = 0;


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

void initializeMemorySpace(int size) {
    lock_guard<mutex> lock(g_memory_space_mutex);
    g_memory_space_size = size;
    g_memory_space.assign(size / 2, 0); // Each uint16 takes 2 bytes
}

bool isValidMemoryAddress(int address) {
    // Check if address is within bounds and is even (uint16 aligned)
    return address >= 0 && address < g_memory_space_size && (address % 2 == 0);
}

uint16_t readMemory(int address) {
    lock_guard<mutex> lock(g_memory_space_mutex);
    if (!isValidMemoryAddress(address)) {
        return 0; //invalid/uninitialized addresses
    }
    int index = address / 2; //byte address to uint16 index
    return g_memory_space[index];
}

void writeMemory(int address, uint16_t value) {
    lock_guard<mutex> lock(g_memory_space_mutex);
    if (!isValidMemoryAddress(address)) {
        stringstream ss;
        ss << "Memory access violation at address 0x" << hex << address << " - invalid address"; //for screen -r etc.
        throw runtime_error(ss.str());
    }
    int index = address / 2; //byte address to uint16 index
    g_memory_space[index] = value;
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

void printMemorySnapshot(const string& filename) {
    lock_guard<mutex> lock(g_memory_mutex);
    ofstream outFile(filename);
    if (!outFile) {
        cerr << "ERROR: Failed to create " << filename << endl;
        return;
    }

    // Calculate statistics
    int process_count = 0;
    int total_fragmentation = 0;
    for (const auto& block : g_memory_blocks) {
        if (!block.is_free) 
            process_count++;
        else 
            total_fragmentation += block.size;
    }

    // Write header
    outFile << "Timestamp: (" << format_timestamp_for_display(time(0)) << ")\n";
    outFile << "Number of processes in memory: " << process_count << "\n";
    outFile << "Total external fragmentation in KB: " << total_fragmentation / 1024 << "\n\n";
    outFile << "----end---- = " << g_max_overall_mem << "\n\n";

    // Print ONLY allocated blocks, from high to low address
    for (auto it = g_memory_blocks.rbegin(); it != g_memory_blocks.rend(); ++it) {
        if (!it->is_free) {
            string pid = it->process_name;
            // Extract Px format if needed
            if (pid.find("_") != string::npos) {
                pid = "P" + pid.substr(pid.rfind("_") + 1);
            }
            outFile << (it->start_address + it->size) << "\n";
            outFile << pid << "\n";
            outFile << it->start_address << "\n\n";
        }
    }

    outFile << "----start---- = 0\n";
    outFile.close();
}