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

// Demand paging structures
vector<Frame> g_physical_frames;
map<string, vector<Page>> g_process_page_tables;
mutex g_paging_mutex;
int g_total_frames = 0;
atomic<int> g_access_counter(0);

// Statistics tracking
atomic<unsigned long long> g_total_cpu_ticks(0);
atomic<unsigned long long> g_idle_cpu_ticks(0);
atomic<unsigned long long> g_active_cpu_ticks(0);
atomic<int> g_pages_paged_in(0);
atomic<int> g_pages_paged_out(0);


void initializeMemory() {
    lock_guard<mutex> lock(g_memory_mutex);
    g_memory_blocks.clear();
    g_memory_blocks.push_back({
        0, 
        g_max_overall_mem, 
        true, 
        ""
    });
    
    // Initialize paging system
    initializePaging();
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

// Demand Paging Implementation
void initializePaging() {
    lock_guard<mutex> lock(g_paging_mutex);
    g_total_frames = g_max_overall_mem / g_mem_per_frame;
    g_physical_frames.clear();
    g_physical_frames.resize(g_total_frames);
    g_process_page_tables.clear();
    g_access_counter = 0;
    
    // Create or clear backing store file
    ofstream backingStore("csopesy-backing-store.txt", ios::trunc);
    if (backingStore.is_open()) {
        backingStore.close();
    }
}

bool allocateMemoryPaging(PCB* process) {
    lock_guard<mutex> lock(g_paging_mutex);
    
    string process_name = process->name;
    int required_pages = calculatePagesRequired(process->memory_requirement);
    
    // Check if process already has page table
    if (g_process_page_tables.find(process_name) != g_process_page_tables.end()) {
        return false; // Already allocated
    }
    
    // Create page table for process (but don't load pages into memory yet)
    vector<Page> page_table(required_pages);
    for (int i = 0; i < required_pages; i++) {
        page_table[i].virtual_page_number = i;
        page_table[i].is_in_memory = false;
        page_table[i].is_dirty = false;
        page_table[i].process_name = process_name;
        page_table[i].physical_frame_number = -1;
        page_table[i].last_access_time = 0;
    }
    
    g_process_page_tables[process_name] = page_table;
    return true; // Always succeeds in demand paging
}

void deallocateMemoryPaging(PCB* process) {
    lock_guard<mutex> lock(g_paging_mutex);
    
    string process_name = process->name;
    
    // Find process page table
    auto it = g_process_page_tables.find(process_name);
    if (it == g_process_page_tables.end()) {
        return; // Process not found
    }
    
    // Free all frames used by this process
    for (auto& page : it->second) {
        if (page.is_in_memory && page.physical_frame_number >= 0) {
            // Save dirty pages to backing store before freeing
            if (page.is_dirty) {
                savePageToBackingStore(process_name, page.virtual_page_number, page.physical_frame_number);
            }
            g_physical_frames[page.physical_frame_number].is_free = true;
            g_physical_frames[page.physical_frame_number].process_name = "";
            g_physical_frames[page.physical_frame_number].virtual_page_number = -1;
        }
    }
    
    // Remove page table
    g_process_page_tables.erase(it);
}

int handlePageFault(const string& process_name, int virtual_page) {
    // This function should be called with g_paging_mutex already locked
    
    // Find a free frame
    int frame_number = findFreeFrame();
    
    if (frame_number == -1) {
        // No free frames, need to evict a page
        frame_number = selectVictimFrame();
        if (frame_number == -1) {
            return -1; // System is deadlocked - no frames can be freed
        }
        
        // Check if we're in a deadlock situation: all frames belong to currently running processes
        // that are trying to access memory, creating a circular wait condition
        bool potential_deadlock = true;
        for (int i = 0; i < g_total_frames; i++) {
            if (g_physical_frames[i].is_free) {
                potential_deadlock = false;
                break;
            }
            
            // Check if this frame belongs to a process that's not currently running
            // If so, we can potentially evict it
            bool belongs_to_running_process = false;
            for (int core = 0; core < config_num_cpu; core++) {
                if (g_running_processes[core] != nullptr && 
                    g_running_processes[core]->name == g_physical_frames[i].process_name) {
                    belongs_to_running_process = true;
                    break;
                }
            }
            
            if (!belongs_to_running_process) {
                potential_deadlock = false;
                break;
            }
        }
        
        // If all frames belong to running processes and we need more frames than available,
        // this creates a deadlock situation
        if (potential_deadlock) {
            // Log deadlock to backing store file instead of console
            ofstream logFile("memory-violation-log.txt", ios::app);
            if (logFile.is_open()) {
                logFile << "[" << getCurrentTimestampWithMillis() << "] DEADLOCK DETECTED: All frames occupied by running processes" << endl;
                logFile.close();
            }
            return -1;
        }
        
        // Save the victim page to backing store if dirty
        Frame& victim_frame = g_physical_frames[frame_number];
        if (!victim_frame.process_name.empty()) {
            auto victim_process_it = g_process_page_tables.find(victim_frame.process_name);
            if (victim_process_it != g_process_page_tables.end()) {
                auto& victim_page_table = victim_process_it->second;
                if (victim_frame.virtual_page_number < victim_page_table.size()) {
                    Page& victim_page = victim_page_table[victim_frame.virtual_page_number];
                    if (victim_page.is_dirty) {
                        savePageToBackingStore(victim_frame.process_name, victim_frame.virtual_page_number, frame_number);
                    }
                    // Mark page as not in memory
                    victim_page.is_in_memory = false;
                    victim_page.physical_frame_number = -1;
                    g_pages_paged_out++; // Increment page-out counter
                }
            }
        }
    }
    
    // Load the required page into the frame
    loadPageFromBackingStore(process_name, virtual_page, frame_number);
    g_pages_paged_in++; // Increment page-in counter
    
    // Update frame information
    g_physical_frames[frame_number].is_free = false;
    g_physical_frames[frame_number].process_name = process_name;
    g_physical_frames[frame_number].virtual_page_number = virtual_page;
    g_physical_frames[frame_number].last_access_time = g_access_counter++;
    
    // Update page table
    auto process_it = g_process_page_tables.find(process_name);
    if (process_it != g_process_page_tables.end() && virtual_page < process_it->second.size()) {
        Page& page = process_it->second[virtual_page];
        page.is_in_memory = true;
        page.physical_frame_number = frame_number;
        page.last_access_time = g_access_counter++;
    }
    
    return frame_number;
}

int findFreeFrame() {
    for (int i = 0; i < g_total_frames; i++) {
        if (g_physical_frames[i].is_free) {
            return i;
        }
    }
    return -1; // No free frame found
}

int selectVictimFrame() {
    // Use LRU (Least Recently Used) replacement algorithm
    int victim_frame = -1;
    int oldest_access_time = INT_MAX;
    
    for (int i = 0; i < g_total_frames; i++) {
        if (!g_physical_frames[i].is_free) {
            if (g_physical_frames[i].last_access_time < oldest_access_time) {
                oldest_access_time = g_physical_frames[i].last_access_time;
                victim_frame = i;
            }
        }
    }
    
    return victim_frame;
}

void loadPageFromBackingStore(const string& process_name, int virtual_page, int frame_number) {
    // Clear the frame (simulate loading from backing store)
    int frame_start = frame_number * g_mem_per_frame / 2; // uint16 index
    int frame_size = g_mem_per_frame / 2; // in uint16 units
    
    // Load page data from backing store file
    ifstream backingStore("csopesy-backing-store.txt");
    string page_key = process_name + "_page_" + to_string(virtual_page);
    bool found = false;
    
    if (backingStore.is_open()) {
        string line;
        while (getline(backingStore, line)) {
            if (line.find(page_key) == 0) {
                // Found the page data, load it
                found = true;
                stringstream ss(line.substr(page_key.length() + 1));
                for (int i = 0; i < frame_size && i + frame_start < g_memory_space.size(); i++) {
                    uint16_t value;
                    if (ss >> value) {
                        g_memory_space[frame_start + i] = value;
                    } else {
                        g_memory_space[frame_start + i] = 0;
                    }
                }
                break;
            }
        }
        backingStore.close();
    }
    
    // If not found in backing store, initialize with zeros
    if (!found) {
        for (int i = 0; i < frame_size && i + frame_start < g_memory_space.size(); i++) {
            g_memory_space[frame_start + i] = 0;
        }
    }
}

void savePageToBackingStore(const string& process_name, int virtual_page, int frame_number) {
    ofstream backingStore("csopesy-backing-store.txt", ios::app);
    if (backingStore.is_open()) {
        string page_key = process_name + "_page_" + to_string(virtual_page);
        backingStore << page_key << ":";
        
        int frame_start = frame_number * g_mem_per_frame / 2; // uint16 index
        int frame_size = g_mem_per_frame / 2; // in uint16 units
        
        for (int i = 0; i < frame_size && i + frame_start < g_memory_space.size(); i++) {
            backingStore << " " << g_memory_space[frame_start + i];
        }
        backingStore << endl;
        backingStore.close();
    }
}

bool isPageInMemory(const string& process_name, int virtual_page) {
    auto process_it = g_process_page_tables.find(process_name);
    if (process_it == g_process_page_tables.end() || virtual_page >= process_it->second.size()) {
        return false;
    }
    return process_it->second[virtual_page].is_in_memory;
}

int getPhysicalAddress(const string& process_name, int virtual_address) {
    int virtual_page = virtual_address / g_mem_per_frame;
    int page_offset = virtual_address % g_mem_per_frame;
    
    if (!isPageInMemory(process_name, virtual_page)) {
        // Page fault occurred - attempt to handle it
        int frame_number = handlePageFault(process_name, virtual_page);
        if (frame_number == -1) {
            // System deadlock - no frames can be freed
            return -1;
        }
    }
    
    // Get the physical frame number
    auto process_it = g_process_page_tables.find(process_name);
    if (process_it == g_process_page_tables.end() || virtual_page >= process_it->second.size()) {
        return -1;
    }
    
    int frame_number = process_it->second[virtual_page].physical_frame_number;
    if (frame_number == -1) {
        return -1;
    }
    
    // Update access time
    g_physical_frames[frame_number].last_access_time = g_access_counter++;
    process_it->second[virtual_page].last_access_time = g_access_counter++;
    
    return frame_number * g_mem_per_frame + page_offset;
}

void printPagingState(const string& context) {
    lock_guard<mutex> lock(g_paging_mutex);
    cerr << "\nPaging State (" << context << "):\n";
    cerr << "Total Frames: " << g_total_frames << "\n";
    
    int free_frames = 0;
    for (int i = 0; i < g_total_frames; i++) {
        if (g_physical_frames[i].is_free) {
            free_frames++;
        } else {
            cerr << "Frame " << i << ": " << g_physical_frames[i].process_name 
                 << " Page " << g_physical_frames[i].virtual_page_number << "\n";
        }
    }
    cerr << "Free Frames: " << free_frames << "\n";
}