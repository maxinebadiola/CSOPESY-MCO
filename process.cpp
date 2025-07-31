#include "headers.h"

// Global variables definitions
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
    g_exit_flag = true;
    g_keep_generating = false;  // This will stop generation
    
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
        g_running_processes[i] = nullptr;
    }
    g_finished_processes.clear();
    g_threads_started = false;
    g_exit_flag = false;
    cout << "Scheduler and process generation stopped successfully." << endl;
}

void schedulerThread() {
    while (!g_exit_flag) {
        PCB* process_to_schedule = nullptr;

        // Get a process from ready queue
        {
            lock_guard<mutex> lock(g_ready_queue_mutex);
            if (!g_ready_queue.empty()) {
                process_to_schedule = g_ready_queue.front();
                g_ready_queue.pop();
            }
        }

        if (process_to_schedule != nullptr) {
            bool scheduled = false;
            
            // Try to assign to an available core
            while (!scheduled && !g_exit_flag) {
                {
                    lock_guard<mutex> lock(g_process_lists_mutex);
                    for (int i = 0; i < config_num_cpu; ++i) {
                        if (g_running_processes[i] == nullptr) {
                            process_to_schedule->state = RUNNING;
                            process_to_schedule->core_id = i;
                            
                            if (current_scheduler_type == RR) {
                                process_to_schedule->remaining_quantum = config_quantum_cycles;
                            }
                            
                            g_running_processes[i] = process_to_schedule;
                            scheduled = true;
                            break;
                        }
                    }
                }
                
                if (!scheduled) {
                    this_thread::sleep_for(chrono::milliseconds(50));
                }
            }
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
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void rr_worker_thread(int core_id) {
    while (!g_exit_flag) {
        PCB* current_process = nullptr;
        {
            lock_guard<mutex> lock(g_process_lists_mutex);
            if (core_id < g_running_processes.size()) {
                current_process = g_running_processes[core_id];
            }
        }

        if (current_process != nullptr) {
            if (current_process->remaining_quantum <= 0) {
                current_process->remaining_quantum = config_quantum_cycles;
                variables.clear();
                variables["var1"] = 0;
                variables["var2"] = 0;
                variables["var3"] = 0;
            }

            if (current_process->instructions_executed < current_process->instructions_total && !g_exit_flag) {
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
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cerr << "Error executing instruction in process " << current_process->name << ": " << e.what() << std::endl;
                }

                current_process->instructions_executed++;
                current_process->remaining_quantum--;
            }

            bool process_finished = (current_process->instructions_executed >= current_process->instructions_total);
            bool quantum_expired = (current_process->remaining_quantum <= 0);

            if (process_finished || quantum_expired) {
                lock_guard<mutex> lock(g_process_lists_mutex);

                if (process_finished) {
                    current_process->state = FINISHED;
                    g_finished_processes.push_back(current_process);
                    g_running_processes[core_id] = nullptr;
                } else if (quantum_expired) {
                    current_process->state = READY;
                    current_process->remaining_quantum = 0;
                    g_running_processes[core_id] = nullptr;

                    {
                        lock_guard<mutex> ready_lock(g_ready_queue_mutex);
                        g_ready_queue.push(current_process);
                    }
                }
            }
        } else {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }
}

void createTestProcesses(const string& screenName) {
    lock_guard<mutex> lock(g_ready_queue_mutex);
    
    // Remove any process count tracking
    for (int i = 0; i < config_batch_process_freq; ++i) {
        // Generate a unique ID using timestamp
        auto now = chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        int process_id = chrono::duration_cast<chrono::milliseconds>(duration).count();
        
        string processName = screenName + "_" + to_string(process_id);
        string filename = "screen_" + processName + ".txt";

        int instructions = rand() % (config_max_ins - config_min_ins + 1) + config_min_ins;

        g_process_storage.push_back(make_unique<PCB>(
            process_id, processName, READY, time(0), instructions, 0, filename, -1
        ));

        g_ready_queue.push(g_process_storage.back().get());
    }
}