// process.cpp
#include "headers.h"
#include <filesystem>  // Required for filesystem operations
namespace fs = std::filesystem;  // Namespace alias for cleaner code

queue<PCB*> g_ready_queue;
mutex g_ready_queue_mutex;
vector<PCB*> g_running_processes;
vector<PCB*> g_finished_processes;
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
atomic<long long> g_idle_cpu_ticks(0);
atomic<long long> g_active_cpu_ticks(0);
atomic<long long> g_pages_paged_in(0);
atomic<long long> g_pages_paged_out(0);
bool debug_mode = false;

void tick_generator_thread() {
    while (!g_exit_flag) {
        this_thread::sleep_for(chrono::milliseconds(TICK_DURATION_MS));
        {
            lock_guard<mutex> lock(g_tick_mutex);
            g_cpu_ticks++;
        }
        g_tick_cv.notify_all();
    }
}

void stopAndResetScheduler() {
    cout << "Stopping scheduler threads..." << endl;
    
    // Signal all threads to stop
    g_exit_flag = true;
    g_keep_generating = false;
    
    // Wake up any waiting threads
    g_tick_cv.notify_all();
    
    //thread exit handling
    this_thread::sleep_for(chrono::milliseconds(100));
    
    //thread timeout
    try {
        if (g_tick_thread.joinable()) {
            g_tick_thread.join();
        }
    } catch (const exception& e) {
        cerr << "Error joining tick thread: " << e.what() << endl;
    }
    
    try {
        if (g_scheduler_thread.joinable()) {
            g_scheduler_thread.join();
        }
    } catch (const exception& e) {
        cerr << "Error joining scheduler thread: " << e.what() << endl;
    }
    
    // Clear worker threads with exception handling
    for (auto& worker : g_worker_threads) {
        try {
            if (worker.joinable()) {
                worker.join();
            }
        } catch (const exception& e) {
            cerr << "Error joining worker thread: " << e.what() << endl;
        }
    }
    g_worker_threads.clear();
    
    // Clean up process queues and memory
    try {
        lock_guard<mutex> lock(g_process_lists_mutex);
        
        // Clear ready queue
        while (!g_ready_queue.empty()) {
            g_ready_queue.pop();
        }
        
        // Deallocate running processes
        for (int i = 0; i < config_num_cpu; ++i) {
            if (g_running_processes[i] != nullptr) {
                try {
                    deallocateMemory(g_running_processes[i]);
                } catch (const exception& e) {
                    cerr << "Error deallocating memory for process " << i << ": " << e.what() << endl;
                }
                g_running_processes[i] = nullptr;
            }
        }
        
        // Clear finished processes
        g_finished_processes.clear();
        
        // Clear process storage
        g_process_storage.clear();
    } catch (const exception& e) {
        cerr << "Error during process cleanup: " << e.what() << endl;
    }
    
    // Reset scheduler state
    g_threads_started = false;
    g_exit_flag = false;  // Reset for next run
    
    // Reset CPU tick counters
    g_cpu_ticks = 0;
    g_idle_cpu_ticks = 0;
    g_active_cpu_ticks = 0;
    
    try {
        closePagingSystem();
    } catch (const exception& e) {
        cerr << "Error closing paging system: " << e.what() << endl;
    }
    
    cout << "Scheduler stopped and reset successfully." << endl;
}

void schedulerThread() {
    try {
        while (!g_exit_flag) {
            PCB* process_to_schedule = nullptr;
            {
                lock_guard<mutex> lock(g_ready_queue_mutex);
                if (!g_ready_queue.empty()) {
                    process_to_schedule = g_ready_queue.front();
                    g_ready_queue.pop();
                    if (debug_mode) {
                        cout << "DEBUG: Scheduler picked up process " << process_to_schedule->name << endl;
                    }
                }
            }
            if (process_to_schedule != nullptr) {
                bool scheduled = false;
                while (!scheduled && !g_exit_flag) {
                    {
                        lock_guard<mutex> lock(g_process_lists_mutex);
                        for (int i = 0; i < config_num_cpu; ++i) {
                            if (g_running_processes[i] == nullptr) {
                                try {
                                    if (allocateMemoryFirstFit(process_to_schedule)) {
                                        process_to_schedule->state = RUNNING;
                                        process_to_schedule->core_id = i;
                                        process_to_schedule->remaining_quantum = config_quantum_cycles;
                                        g_running_processes[i] = process_to_schedule;
                                        if (debug_mode) {
                                            cout << "DEBUG: Process " << process_to_schedule->name << " scheduled to core " << i << endl;
                                        }
                                        scheduled = true;
                                        break;
                                    } else {
                                        if (debug_mode) {
                                            cout << "DEBUG: Failed to allocate memory for process " << process_to_schedule->name << " on core " << i << endl;
                                        }
                                    }
                                } catch (const exception& e) {
                                    cerr << "Memory allocation error: " << e.what() << endl;
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
            } else {
                this_thread::sleep_for(chrono::milliseconds(100));
            }
        }
    } catch (const exception& e) {
        cerr << "Scheduler thread crashed: " << e.what() << endl;
    } catch (...) {
        cerr << "Scheduler thread crashed with unknown exception" << endl;
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
            // ACTIVE: Core is executing a process
            g_active_cpu_ticks++;
            
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
                    
                    // Increment active ticks for each CPU tick spent executing
                    g_active_cpu_ticks++;
                }
                
                if (g_exit_flag) break;
                
                // Simulate memory access for paging on EVERY instruction
                simulateMemoryAccess(current_process->name);
                
                std::vector<std::string> singleInstruction = generateRandomInstructions(
                    current_process->name, 1, enable_sleep, enable_for);
                try {
                    executeInstructionSet(singleInstruction, 0, current_process);
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cerr << "Error executing instruction in process " << current_process->name << ": " << e.what() << std::endl;
                }
                current_process->instructions_executed++;
            }
            
            if (!g_exit_flag) {
                lock_guard<mutex> lock(g_process_lists_mutex);
                current_process->state = FINISHED;
                g_finished_processes.push_back(current_process);
                g_running_processes[core_id] = nullptr;
            }
        } else {
            // IDLE: Core has no process to execute
            g_idle_cpu_ticks++;
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void rr_worker_thread(int core_id) {
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
            // ACTIVE: Core is executing a process
            g_active_cpu_ticks++;
            
            // 1. Handle quantum expiration
            current_process->remaining_quantum--;
            
            if (current_process->remaining_quantum <= 0) {
                // Reset quantum
                current_process->remaining_quantum = config_quantum_cycles;
                
                // Reset process variables
                variables.clear();
                variables["var1"] = 0;
                variables["var2"] = 0;
                variables["var3"] = 0;
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
                    
                    // Increment active ticks for each CPU tick spent executing
                    g_active_cpu_ticks++;
                }
                
                if (!g_exit_flag.load()) {
                    // Simulate memory access for paging on EVERY instruction
                    simulateMemoryAccess(current_process->name);
                    
                    try {
                        auto instruction = generateRandomInstructions(
                            current_process->name, 1, enable_sleep, enable_for);
                        executeInstructionSet(instruction, 0, current_process);
                    } catch (const exception& e) {
                        lock_guard<mutex> lock(outputMutex);
                        cerr << "Core " << core_id << ": Error in " 
                             << current_process->name << " - " << e.what() << endl;
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
                deallocateMemory(current_process);
                
                // Prevent finished processes from accumulating indefinitely
                if (g_finished_processes.size() > 100) {
                    g_finished_processes.erase(g_finished_processes.begin(), 
                                               g_finished_processes.begin() + 50);
                }
                
                if (debug_mode) {
                    cout << "DEBUG: Process " << current_process->name << " finished on core " << core_id << endl;
                }
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
                cerr << "Core " << core_id << ": " 
                     << current_process->name << " requeued\n";
            }
        } else {
            // IDLE: Core has no process to execute
            g_idle_cpu_ticks++;
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
        
        // Use configured instruction count range
        int instruction_count = config_min_ins + (rand() % (config_max_ins - config_min_ins + 1));
        
        auto new_pcb = make_unique<PCB>(
            process_counter - 1, // Use counter as ID
            processName,
            READY, 
            time(0), 
            instruction_count, // Use configured instruction count
            0,
            filename,
            -1,
            mem_needed
        );
        
        if (debug_mode) {
            cout << "DEBUG: Created process " << processName << " with " << mem_needed << " bytes memory requirement" << endl;
        }
        g_process_storage.push_back(std::move(new_pcb));
        g_ready_queue.push(g_process_storage.back().get());
    }
}