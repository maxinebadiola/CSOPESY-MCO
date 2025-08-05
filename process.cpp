// process.cpp
#include "headers.h"
#include <filesystem>  // Required for filesystem operations
namespace fs = std::filesystem;  // Namespace alias for cleaner code

queue<PCB*> g_ready_queue;
mutex g_ready_queue_mutex;
vector<PCB*> g_running_processes;
vector<PCB*> g_finished_processes;

// Cancelled processes (memory violations)
vector<CancelledProcess> g_cancelled_processes;
mutex g_cancelled_processes_mutex;

mutex g_process_lists_mutex;
atomic<bool> g_exit_flag(false);
vector<unique_ptr<PCB>> g_process_storage;
thread g_scheduler_thread;
vector<thread> g_worker_threads;
atomic<bool> g_threads_started(false);
const int TICK_DURATION_MS = 10;
atomic<unsigned long long> g_cpu_ticks(0);
mutex g_tick_mutex;
condition_variable g_tick_cv;
thread g_tick_thread;
atomic<bool> g_keep_generating(false);

void tick_generator_thread() {
    while (!g_exit_flag) {
        this_thread::sleep_for(chrono::milliseconds(TICK_DURATION_MS));
        {
            lock_guard<mutex> lock(g_tick_mutex);
            g_cpu_ticks++;
            
            // Count active and idle cores for statistics
            int active_cores = 0;
            {
                lock_guard<mutex> process_lock(g_process_lists_mutex);
                for (int i = 0; i < config_num_cpu; ++i) {
                    if (g_running_processes[i] != nullptr) {
                        active_cores++;
                    }
                }
            }
            
            // Update CPU tick statistics
            g_total_cpu_ticks += config_num_cpu; // Each tick affects all cores
            g_active_cpu_ticks += active_cores;
            g_idle_cpu_ticks += (config_num_cpu - active_cores);
        }
        g_tick_cv.notify_all();
    }
}

void stopAndResetScheduler() {
    g_exit_flag = true;
    g_keep_generating = false;
    if (g_tick_thread.joinable()) {
        g_tick_thread.join();
    }
    if (g_scheduler_thread.joinable()) {
        g_scheduler_thread.join();
    }
    for (auto& worker : g_worker_threads) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    lock_guard<mutex> lock(g_process_lists_mutex);
    while (!g_ready_queue.empty()) {
        g_ready_queue.pop();
    }
    for (int i = 0; i < config_num_cpu; ++i) {
        if (g_running_processes[i] != nullptr) {
            deallocateMemoryPaging(g_running_processes[i]);  // Cleanup any running processes
        }
        g_running_processes[i] = nullptr;
    }
    g_finished_processes.clear();
    
    {
        lock_guard<mutex> lock(g_cancelled_processes_mutex);
        g_cancelled_processes.clear();
    }
    
    // Reset statistics
    g_total_cpu_ticks = 0;
    g_idle_cpu_ticks = 0;
    g_active_cpu_ticks = 0;
    g_pages_paged_in = 0;
    g_pages_paged_out = 0;
    
    g_threads_started = false;
    g_exit_flag = false;
    cout << "Scheduler and process generation stopped successfully." << endl;
}

void schedulerThread() {
    while (!g_exit_flag) {
        PCB* process_to_schedule = nullptr;
        {
            lock_guard<mutex> lock(g_ready_queue_mutex);
            if (!g_ready_queue.empty()) {
                process_to_schedule = g_ready_queue.front();
                g_ready_queue.pop();
            }
        }
        if (process_to_schedule != nullptr) {
            // 🔽 COMMENT OUT THESE TWO LINES TO STOP LOG SPAM 🔽
            // printMemoryState("Before allocation");
            // printMemoryState("After allocation");

            bool scheduled = false;
            while (!scheduled && !g_exit_flag) {
                {
                    lock_guard<mutex> lock(g_process_lists_mutex);
                    for (int i = 0; i < config_num_cpu; ++i) {
                        if (g_running_processes[i] == nullptr) {
                            if (allocateMemoryPaging(process_to_schedule)) {
                                process_to_schedule->state = RUNNING;
                                process_to_schedule->core_id = i;
                                process_to_schedule->remaining_quantum = config_quantum_cycles;
                                g_running_processes[i] = process_to_schedule;
                                scheduled = true;
                                break;
                            }
                        }
                    }
                }
                if (!scheduled) {
                    {
                        lock_guard<mutex> lock(g_ready_queue_mutex);
                        g_ready_queue.push(process_to_schedule);
                    }
                    this_thread::sleep_for(chrono::milliseconds(50));
                    break;
                }
            }
            // 🔽 COMMENT OUT THIS LINE TOO 🔽
            // printMemoryState("After allocation");
        } else {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void fcfs_worker_thread(int core_id) {
    while (!g_exit_flag) {
        PCB* current_process = nullptr;
        {
            lock_guard<mutex> lock(g_process_lists_mutex);
            if (core_id < g_running_processes.size()) {
                current_process = g_running_processes[core_id];
            }
        }
        if (current_process != nullptr) {
            // Initialize process-specific symbol table
            current_process->symbol_table.clear();
            
            // If process has custom instructions, execute them instead of random ones
            if (current_process->has_custom_instructions) {
                try {
                    executeInstructionSet(current_process->custom_instructions, 0, current_process);
                    current_process->instructions_executed = current_process->instructions_total;
                } catch (const exception& e) {
                    // Handle memory access violation by terminating the process
                    string error_msg = e.what();
                    bool is_memory_violation = error_msg.find("Memory access violation") != string::npos;
                    
                    if (is_memory_violation) {
                        // Extract memory address from error message
                        string mem_address = "unknown";
                        size_t addr_pos = error_msg.find("address ");
                        if (addr_pos != string::npos) {
                            mem_address = error_msg.substr(addr_pos + 8);
                            // Remove any trailing text after the address
                            size_t space_pos = mem_address.find(' ');
                            if (space_pos != string::npos) {
                                mem_address = mem_address.substr(0, space_pos);
                            }
                        }
                        
                        // Add to cancelled processes list
                        CancelledProcess cancelled;
                        cancelled.process = current_process;
                        cancelled.timestamp = format_timestamp_for_display(time(0));
                        cancelled.time_only = getTimeOnlyFromTimestamp(getCurrentTimestampWithMillis());
                        cancelled.memory_address = mem_address;
                        
                        // Log to memory violation file
                        ofstream logFile("memory-violation-log.txt", ios::app);
                        if (logFile.is_open()) {
                            logFile << "[" << getCurrentTimestampWithMillis() << "] Process " << current_process->name 
                                    << " terminated due to memory access violation at address " << mem_address << endl;
                            logFile.close();
                        }
                        
                        {
                            lock_guard<mutex> lock(g_cancelled_processes_mutex);
                            g_cancelled_processes.push_back(cancelled);
                        }
                    }
                    
                    // Don't print memory violations to console, only log them
                    current_process->state = FINISHED;
                    current_process->instructions_executed = current_process->instructions_total;
                }
            } else {
                // Original random instruction execution
                variables.clear();
                variables["var1"] = 0;
                variables["var2"] = 0;
                variables["var3"] = 0;
                while (current_process->instructions_executed < current_process->instructions_total && !g_exit_flag) {
                    for (int tick_count = 0; tick_count < config_delay_per_exec; ++tick_count) {
                        if (g_exit_flag) break;
                        unsigned long long last_known_tick = g_cpu_ticks.load();
                        unique_lock<mutex> lock(g_tick_mutex);
                        g_tick_cv.wait(lock, [&]{
                            return g_cpu_ticks.load() > last_known_tick || g_exit_flag.load();
                        });
                    }
                    if (g_exit_flag) break;
                    std::vector<std::string> singleInstruction = generateRandomInstructions(
                        current_process->name, 1, enable_sleep, enable_for);
                    try {
                        executeInstructionSet(singleInstruction, 0, current_process);
                    } catch (const std::exception& e) {
                        // Don't print random instruction errors to console, only log them
                        // Memory violations will be handled by the instruction execution system
                    }
                    current_process->instructions_executed++;
                }
            }
            
            if (!g_exit_flag) {
                lock_guard<mutex> lock(g_process_lists_mutex);
                current_process->state = FINISHED;
                g_finished_processes.push_back(current_process);
                g_running_processes[core_id] = nullptr;
                deallocateMemoryPaging(current_process);
            }
        } else {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void rr_worker_thread(int core_id) {
    //Static one-time directory creation (DISABLED)
    static std::once_flag dir_created;
    std::call_once(dir_created, [](){
        const string snapshot_dir = "memory_snapshots";
        try {
            if (!fs::exists(snapshot_dir)) {
                fs::create_directory(snapshot_dir);
            }
        } catch (const fs::filesystem_error& e) {
            cerr << "FATAL: Directory creation failed - " << e.what() << endl;
            exit(1);
        }
    });

    // Thread-local counter for sequential filenames
    static thread_local int thread_file_counter = 0;
    
    while (!g_exit_flag.load()) {
        PCB* current_process = nullptr;
        
        // Get current process for this core
        {
            lock_guard<mutex> lock(g_process_lists_mutex);
            if (core_id < static_cast<int>(g_running_processes.size())) {
                current_process = g_running_processes[core_id];
            }
        }

        if (current_process != nullptr) {
            // 1. Handle quantum expiration
            current_process->remaining_quantum--;
            
            if (current_process->remaining_quantum <= 0) {
                // Reset quantum
                current_process->remaining_quantum = config_quantum_cycles;
                
                // Generate unique filename
                string filename = "memory_snapshots/memory_stamp_" + 
                                to_string(core_id) + "_" +
                                to_string(thread_file_counter++) + ".txt";
                
                //Take memory snapshot (with thread-safe file access)
                {
                    lock_guard<mutex> file_lock(outputMutex);
                    printMemorySnapshot(filename);
                    // cerr << "Core " << core_id << ": Created " << filename << endl;
                }
                
                // Reset process variables for both custom and random processes
                if (current_process->has_custom_instructions) {
                    current_process->symbol_table.clear();
                } else {
                    variables.clear();
                    variables["var1"] = 0;
                    variables["var2"] = 0;
                    variables["var3"] = 0;
                }
            }

            // 2. Execute process instructions
            if (current_process->instructions_executed < current_process->instructions_total && !g_exit_flag.load()) {
                for (int tick_count = 0; tick_count < config_delay_per_exec; ++tick_count) {
                    if (g_exit_flag.load()) break;
                    
                    unsigned long long last_known_tick = g_cpu_ticks.load();
                    unique_lock<mutex> lock(g_tick_mutex);
                    g_tick_cv.wait(lock, [&]{
                        return g_cpu_ticks.load() > last_known_tick || g_exit_flag.load();
                    });
                }
                
                if (!g_exit_flag.load()) {
                    try {
                        if (current_process->has_custom_instructions) {
                            // Execute custom instructions one by one
                            if (current_process->instructions_executed < current_process->custom_instructions.size()) {
                                vector<string> single_instruction = {
                                    current_process->custom_instructions[current_process->instructions_executed]
                                };
                                executeInstructionSet(single_instruction, 0, current_process);
                            }
                        } else {
                            // Execute random instructions
                            auto instruction = generateRandomInstructions(
                                current_process->name, 1, enable_sleep, enable_for);
                            executeInstructionSet(instruction, 0, current_process);
                        }
                    } catch (const exception& e) {
                        string error_msg = e.what();
                        bool is_memory_violation = error_msg.find("Memory access violation") != string::npos;
                        
                        if (is_memory_violation) {
                            // Extract memory address from error message
                            string mem_address = "unknown";
                            size_t addr_pos = error_msg.find("address ");
                            if (addr_pos != string::npos) {
                                mem_address = error_msg.substr(addr_pos + 8);
                                // Remove any trailing text after the address
                                size_t space_pos = mem_address.find(' ');
                                if (space_pos != string::npos) {
                                    mem_address = mem_address.substr(0, space_pos);
                                }
                            }
                            
                            // Add to cancelled processes list
                            CancelledProcess cancelled;
                            cancelled.process = current_process;
                            cancelled.timestamp = format_timestamp_for_display(time(0));
                            cancelled.time_only = getTimeOnlyFromTimestamp(getCurrentTimestampWithMillis());
                            cancelled.memory_address = mem_address;
                            
                            // Log to memory violation file
                            ofstream logFile("memory-violation-log.txt", ios::app);
                            if (logFile.is_open()) {
                                logFile << "[" << getCurrentTimestampWithMillis() << "] Process " << current_process->name 
                                        << " terminated due to memory access violation at address " << mem_address << endl;
                                logFile.close();
                            }
                            
                            {
                                lock_guard<mutex> lock(g_cancelled_processes_mutex);
                                g_cancelled_processes.push_back(cancelled);
                            }
                        }
                        
                        // Don't print memory violations to console, only log them
                        current_process->state = FINISHED;
                        current_process->instructions_executed = current_process->instructions_total;
                    }
                    current_process->instructions_executed++;
                }
            }

            // 3. Handle process state transitions
            bool process_finished = (current_process->instructions_executed >= 
                                    current_process->instructions_total);
            bool quantum_expired = (current_process->remaining_quantum <= 0);

            if (process_finished) {
                lock_guard<mutex> lock(g_process_lists_mutex);
                current_process->state = FINISHED;
                g_finished_processes.push_back(current_process);
                g_running_processes[core_id] = nullptr;
                deallocateMemoryPaging(current_process);
                // cerr << "Core " << core_id << ": " 
                //      << current_process->name << " FINISHED\n";
            }
            else if (quantum_expired) {
                lock_guard<mutex> lock(g_process_lists_mutex);
                current_process->state = READY;
                g_running_processes[core_id] = nullptr;
                
                {
                    lock_guard<mutex> ready_lock(g_ready_queue_mutex);
                    g_ready_queue.push(current_process);
                }
                // Don't print requeue messages to console
            }
        } else {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }
}

void createTestProcesses(const string& screenName) {
    lock_guard<mutex> lock(g_ready_queue_mutex);
    static int process_counter = 1; // Add this static counter
    
    for (int i = 0; i < config_batch_process_freq; ++i) {
        // Calculate random memory requirement between min and max
        int mem_needed = g_min_mem_per_proc + (rand() % (g_max_mem_per_proc - g_min_mem_per_proc + 1));
        
        string processName = "P" + to_string(process_counter++); // Use sequential counter
        string filename = "screen_" + processName + ".txt";
        
        auto new_pcb = make_unique<PCB>(
            process_counter - 1, // Use counter as ID
            processName,
            READY, 
            time(0), 
            rand() % 50 + 10, // 10-60 instructions
            0,
            filename,
            -1,
            mem_needed
        );
        
        g_process_storage.push_back(std::move(new_pcb));
        g_ready_queue.push(g_process_storage.back().get());
    }
}