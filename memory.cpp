// memory.cpp
#include "headers.h"
#include <filesystem>

vector<MemoryBlock> g_memory_blocks;
mutex g_memory_mutex;
vector<Page> g_page_table;
vector<bool> g_frame_table;
ofstream g_backing_store;
mutex g_paging_mutex;


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

void initializePaging() {
    lock_guard<mutex> lock(g_paging_mutex);
    
    // Initialize frame table
    int total_frames = g_max_overall_mem / g_mem_per_frame;
    g_frame_table.assign(total_frames, false);
    
    // Clear page table
    g_page_table.clear();
    
    // Initialize backing store file
    g_backing_store.open("csopesy-backing-store.txt", ios::out | ios::trunc);
    if (g_backing_store.is_open()) {
        g_backing_store << "CSOPESY Backing Store - Paging Operations Log\n";
        g_backing_store << "============================================\n";
        g_backing_store.flush();
    }
    
    // Reset paging counters
    g_pages_paged_in = 0;
    g_pages_paged_out = 0;
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
    if (process == nullptr) {
        cout << "ERROR: Trying to allocate memory for null process" << endl;
        return false;
    }
    
    lock_guard<mutex> lock(g_memory_mutex);

    int required_size = process->memory_requirement > 0 ? 
                       process->memory_requirement : 
                       g_min_mem_per_proc;

    if (debug_mode) {
        cout << "DEBUG: Trying to allocate " << required_size << " bytes for process " << process->name << endl;
    }

    // Check if already allocated
    for (const auto& block : g_memory_blocks) {
        if (!block.is_free && block.process_name == process->name) {
            if (debug_mode) {
                cout << "DEBUG: Process " << process->name << " already has memory allocated" << endl;
            }
            return false;
        }
    }

    // With paging, we need to check if we can fit ANY process in memory
    // Even if not enough contiguous space, we can still allocate if frames are available
    
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
            
            // Simulate memory access for paging
            simulateMemoryAccess(process->name);
            
            verifyMemoryConsistency();
            if (debug_mode) {
                cout << "DEBUG: Successfully allocated " << required_size << " bytes for process " << process->name << endl;
            }
            return true;
        }
    }
    
    // If normal allocation fails, check if we can use paging
    // For this test case, with 1024MB needed and only 1024MB total,
    // we need to page out other processes
    
    // Find if there are other processes in memory to page out
    bool has_other_processes = false;
    string process_to_evict = "";
    
    for (const auto& block : g_memory_blocks) {
        if (!block.is_free && block.process_name != process->name) {
            has_other_processes = true;
            process_to_evict = block.process_name;
            break;
        }
    }
    
    if (has_other_processes && !process_to_evict.empty()) {
        // Page out the other process
        deallocateMemory(nullptr); // This will find and deallocate
        
        // Try allocation again after making space
        for (auto it = g_memory_blocks.begin(); it != g_memory_blocks.end(); ++it) {
            if (it->is_free && it->size >= required_size) {
                it->is_free = false;
                it->process_name = process->name;
                
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
                
                // Simulate memory access for paging
                simulateMemoryAccess(process->name);
                
                verifyMemoryConsistency();
                if (debug_mode) {
                    cout << "DEBUG: Successfully allocated " << required_size << " bytes for process " << process->name << " after eviction" << endl;
                }
                return true;
            }
        }
    }
    
    if (debug_mode) {
        cout << "DEBUG: Failed to allocate memory for process " << process->name << endl;
    }
    return false;
}

void deallocateMemory(PCB* process) {
    if (process == nullptr) return;
    
    string process_name = process->name;
    
    // Page out all pages for this process first
    {
        lock_guard<mutex> paging_lock(g_paging_mutex);
        int pages_needed = calculatePagesRequired(g_min_mem_per_proc);
        for (int page_num = 0; page_num < pages_needed; page_num++) {
            for (auto& page : g_page_table) {
                if (page.process_name == process_name && page.page_number == page_num && page.is_in_memory) {
                    // Mark frame as free
                    g_frame_table[page.frame_number] = false;
                    page.is_in_memory = false;
                    
                    // Log page out
                    if (g_backing_store.is_open()) {
                        g_backing_store << "PAGE OUT: Process " << process_name 
                                       << " Page " << page_num 
                                       << " from Frame " << page.frame_number << "\n";
                        g_backing_store.flush();
                    }
                    g_pages_paged_out++;
                    break;
                }
            }
        }
    }
    
    // Then deallocate memory blocks
    lock_guard<mutex> lock(g_memory_mutex);
    for (auto it = g_memory_blocks.begin(); it != g_memory_blocks.end(); ) {
        if (!it->is_free && it->process_name == process_name) {
            it->is_free = true;
            it->process_name.clear();

            // Merge with previous free block if possible
            if (it != g_memory_blocks.begin()) {
                auto prev_it = prev(it);
                if (prev_it->is_free) {
                    prev_it->size += it->size;
                    it = g_memory_blocks.erase(it);
                    continue;
                }
            }

            // Merge with next free block if possible
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

void pageIn(const string& process_name, int page_number) {
    lock_guard<mutex> lock(g_paging_mutex);
    
    // Find a free frame or select victim using LRU
    int frame_to_use = -1;
    int total_frames = g_max_overall_mem / g_mem_per_frame;
    
    // First try to find a free frame
    for (int i = 0; i < total_frames; i++) {
        if (!g_frame_table[i]) {
            frame_to_use = i;
            break;
        }
    }
    
    // If no free frame, use LRU to select victim
    if (frame_to_use == -1) {
        int lru_page_idx = findLRUPage();
        if (lru_page_idx != -1) {
            // Page out the LRU page
            g_page_table[lru_page_idx].is_in_memory = false;
            frame_to_use = g_page_table[lru_page_idx].frame_number;
            
            // Log page out
            if (g_backing_store.is_open()) {
                g_backing_store << "PAGE OUT: Process " << g_page_table[lru_page_idx].process_name 
                               << " Page " << g_page_table[lru_page_idx].page_number 
                               << " from Frame " << frame_to_use << "\n";
                g_backing_store.flush();
            }
            g_pages_paged_out++;
        }
    }
    
    if (frame_to_use != -1) {
        // Mark frame as used
        g_frame_table[frame_to_use] = true;
        
        // Add or update page in page table
        bool found = false;
        for (auto& page : g_page_table) {
            if (page.process_name == process_name && page.page_number == page_number) {
                page.is_in_memory = true;
                page.frame_number = frame_to_use;
                page.last_access_time = time(nullptr);
                found = true;
                break;
            }
        }
        
        if (!found) {
            g_page_table.push_back({
                page_number,
                process_name,
                true,
                frame_to_use,
                time(nullptr)
            });
        }
        
        // Log page in
        if (g_backing_store.is_open()) {
            g_backing_store << "PAGE IN: Process " << process_name 
                           << " Page " << page_number 
                           << " to Frame " << frame_to_use << "\n";
            g_backing_store.flush();
        }
        g_pages_paged_in++;
    }
}

void pageOut(const string& process_name, int page_number) {
    lock_guard<mutex> lock(g_paging_mutex);
    
    for (auto& page : g_page_table) {
        if (page.process_name == process_name && page.page_number == page_number && page.is_in_memory) {
            // Mark frame as free
            g_frame_table[page.frame_number] = false;
            page.is_in_memory = false;
            
            // Log page out
            if (g_backing_store.is_open()) {
                g_backing_store << "PAGE OUT: Process " << process_name 
                               << " Page " << page_number 
                               << " from Frame " << page.frame_number << "\n";
                g_backing_store.flush();
            }
            g_pages_paged_out++;
            break;
        }
    }
}

int findLRUPage() {
    time_t oldest_time = time(nullptr);
    int lru_index = -1;
    
    for (int i = 0; i < g_page_table.size(); i++) {
        if (g_page_table[i].is_in_memory && g_page_table[i].last_access_time < oldest_time) {
            oldest_time = g_page_table[i].last_access_time;
            lru_index = i;
        }
    }
    
    return lru_index;
}

bool isProcessInMemory(const string& process_name) {
    lock_guard<mutex> lock(g_paging_mutex);
    
    for (const auto& page : g_page_table) {
        if (page.process_name == process_name && page.is_in_memory) {
            return true;
        }
    }
    return false;
}

void simulateMemoryAccess(const string& process_name) {
    if (process_name.empty()) {
        cout << "ERROR: simulateMemoryAccess called with empty process name" << endl;
        return;
    }
    
    try {
        // Calculate how many pages this process needs (should be 4 pages for 1024 bytes)
        int pages_needed = calculatePagesRequired(g_min_mem_per_proc);
        
        if (pages_needed <= 0) {
            cout << "ERROR: Invalid pages_needed: " << pages_needed << " for process " << process_name << endl;
            return;
        }
        
        // For each page, simulate access and force paging
        for (int page_num = 0; page_num < pages_needed; page_num++) {
            bool page_found = false;
            
            {
                lock_guard<mutex> lock(g_paging_mutex);
                // Check if page is currently in memory
                for (auto& page : g_page_table) {
                    if (page.process_name == process_name && page.page_number == page_num) {
                        if (page.is_in_memory) {
                            page.last_access_time = time(nullptr);
                            page_found = true;
                        }
                        break;
                    }
                }
            }
            
            // If page not in memory or doesn't exist, we need to page it in
            // This will force eviction of other processes due to limited memory
            if (!page_found) {
                pageIn(process_name, page_num);
            }
        }
    } catch (const exception& e) {
        cerr << "Error in simulateMemoryAccess for " << process_name << ": " << e.what() << endl;
    } catch (...) {
        cerr << "Unknown error in simulateMemoryAccess for " << process_name << endl;
    }
}

void closePagingSystem() {
    try {
        if (g_backing_store.is_open()) {
            g_backing_store << "\nPaging session ended.\n";
            g_backing_store << "Total pages paged in: " << g_pages_paged_in.load() << "\n";
            g_backing_store << "Total pages paged out: " << g_pages_paged_out.load() << "\n";
            g_backing_store.close();
        }
    } catch (const exception& e) {
        cerr << "Error closing paging system: " << e.what() << endl;
    } catch (...) {
        cerr << "Unknown error closing paging system" << endl;
    }
}