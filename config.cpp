// config.cpp

#include "headers.h"

int config_num_cpu;
string config_scheduler;
int config_quantum_cycles;
int config_batch_process_freq;
int config_min_ins;
int config_max_ins;
int config_delay_per_exec = 4;  // Default to 4 ticks per instruction
int g_max_overall_mem;
int g_mem_per_frame;
int g_min_mem_per_proc;
int g_max_mem_per_proc;
SchedulerType current_scheduler_type;

// Declare extern variables for demand paging
extern vector<Frame> g_physical_frames;
extern map<string, vector<Page>> g_process_page_tables;
extern mutex g_paging_mutex;
extern int g_total_frames;
extern atomic<int> g_access_counter;

// Statistics tracking
extern atomic<unsigned long long> g_total_cpu_ticks;
extern atomic<unsigned long long> g_idle_cpu_ticks;
extern atomic<unsigned long long> g_active_cpu_ticks;
extern atomic<int> g_pages_paged_in;
extern atomic<int> g_pages_paged_out;

void readConfigFile() {
    ifstream configFile("config.txt");
    string key;
    while (configFile >> key) {
        if (key == "num-cpu") {
            configFile >> config_num_cpu;
        } else if (key == "scheduler") {
            string sched;
            configFile >> sched;
            if (sched.front() == '"' && sched.back() == '"') {
                sched = sched.substr(1, sched.length() - 2);
            }
            config_scheduler = sched;
            if (sched == "fcfs" || sched == "FCFS") {
                current_scheduler_type = FCFS;
            } else if (sched == "rr" || sched == "RR") {
                current_scheduler_type = RR;
            } else {
                current_scheduler_type = FCFS;
                cout << "Warning: Unknown scheduler type '" << sched << "', defaulting to FCFS" << endl;
            }
        } else if (key == "quantum-cycles") {
            configFile >> config_quantum_cycles;
        } else if (key == "batch-process-freq") {
            configFile >> config_batch_process_freq;
        } else if (key == "min-ins") {
            configFile >> config_min_ins;
        } else if (key == "max-ins") {
            configFile >> config_max_ins;
        } else if (key == "delay-per-exec") {
            configFile >> config_delay_per_exec;
        } else if (key == "max-overall-mem") {
            configFile >> g_max_overall_mem;
        } else if (key == "mem-per-frame") {
            configFile >> g_mem_per_frame;
        } else if (key == "min-mem-per-proc") {
            configFile >> g_min_mem_per_proc;
        } else if (key == "max-mem-per-proc") {
            configFile >> g_max_mem_per_proc;
        } else {
            string skip;
            configFile >> skip;
        }
    }
    initializeMemory();
    initializeMemorySpace(g_max_overall_mem); //use config value
    configFile.close();
}

void printConfigVars() {
    cout << "\n[CONFIG VALUES LOADED]" << endl;
    cout << "num-cpu: " << config_num_cpu << endl;
    cout << "scheduler: " << config_scheduler << " (" << (current_scheduler_type == FCFS ? "FCFS" : "Round Robin") << ")" << endl;
    cout << "quantum-cycles: " << config_quantum_cycles << endl;
    cout << "batch-process-freq: " << config_batch_process_freq << endl;
    cout << "min-ins: " << config_min_ins << endl;
    cout << "max-ins: " << config_max_ins << endl;
    cout << "delay-per-exec: " << config_delay_per_exec << " ticks" << endl;
    cout << "max-overall-mem: " << g_max_overall_mem << " bytes" << endl;
    cout << "mem-per-frame: " << g_mem_per_frame << " bytes" << endl;
    cout << "min-mem-per-proc: " << g_min_mem_per_proc << " bytes" << endl;
    cout << "max-mem-per-proc: " << g_max_mem_per_proc << " bytes" << endl;
    cout << "total-frames: " << (g_max_overall_mem / g_mem_per_frame) << endl;
    cout << "[System Info] Tick Duration: " << TICK_DURATION_MS << " ms" << endl;
}