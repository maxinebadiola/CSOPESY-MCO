#include "headers.h"
#include <iomanip>

string getCurrentTimestampWithMillis() {
    auto now = chrono::system_clock::now();
    auto now_ms = chrono::time_point_cast<chrono::milliseconds>(now);
    auto epoch = now_ms.time_since_epoch();
    auto value = chrono::duration_cast<chrono::milliseconds>(epoch);
    time_t now_c = chrono::system_clock::to_time_t(now);
    
    tm localTime;
    #ifdef _WIN64
    localtime_s(&localTime, &now_c);
    #else
    localtime_r(&now_c, &localTime);
    #endif
    
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%m/%d/%Y %I:%M:%S", &localTime);
    
    stringstream ss;
    ss << buffer << "." << setfill('0') << setw(3) << value.count() % 1000;
    ss << (localTime.tm_hour >= 12 ? "PM" : "AM");
    
    return ss.str();
}

string getTimeOnlyFromTimestamp(const string& timestamp) {
    // Extract time from timestamp (format: MM/DD/YYYY HH:MM:SS.mmmAM/PM)
    size_t spacePos = timestamp.find(' ');
    if (spacePos != string::npos && spacePos + 1 < timestamp.length()) {
        string timeWithMs = timestamp.substr(spacePos + 1);
        // Remove milliseconds if present
        size_t dotPos = timeWithMs.find('.');
        if (dotPos != string::npos) {
            string timeOnly = timeWithMs.substr(0, dotPos);
            string ampm = timeWithMs.substr(timeWithMs.length() - 2);
            return timeOnly + ampm;
        }
        return timeWithMs;
    }
    return "unknown";
}

string format_timestamp_for_display(time_t t) {
    tm local_tm;
#ifdef _WIN64
    localtime_s(&local_tm, &t);
#else
    localtime_r(&t, &local_tm);
#endif
    
    stringstream ss;
    ss << put_time(&local_tm, "(%m/%d/%Y, %I:%M:%S %p)");
    return ss.str();
}

string getSystemReport() {
    lock_guard<mutex> lock(g_process_lists_mutex);
    stringstream ss;

    ss << "==== CPU UTILIZATION REPORT ====\n";
    int used_cores = 0;
    for (int i = 0; i < config_num_cpu; ++i) {
        if (g_running_processes[i] != nullptr) used_cores++;
    }

    double cpu_utilization = (static_cast<double>(used_cores) / config_num_cpu) * 100.0;
    ss << fixed << setprecision(1);
    ss << "CPU Utilization: " << cpu_utilization << "%\n";
    ss << "Current CPU Tick: " << g_cpu_ticks.load() << "\n"; 
    ss << "Cores Used: " << used_cores << endl;
    ss << "Cores available: " << (config_num_cpu - used_cores) << endl;
    ss << "Scheduler: " << (current_scheduler_type == FCFS ? "First-Come-First-Served (FCFS)" : "Round Robin (RR)");
    if (current_scheduler_type == RR) {
        ss << " [Quantum: " << config_quantum_cycles << " cycles]";
    }
    ss << endl;

    int ready_count = 0;
    {
        lock_guard<mutex> ready_lock(g_ready_queue_mutex);
        queue<PCB*> temp_queue = g_ready_queue;
        while (!temp_queue.empty()) {
            ready_count++;
            temp_queue.pop();
        }
    }
    ss << "Processes in Ready Queue: " << ready_count << endl;

    ss << "\n==== RUNNING PROCESSES ====\n";
    bool anyRunning = false;
    for (int i = 0; i < config_num_cpu; ++i) {
        PCB* p = g_running_processes[i];
        if (p != nullptr) {
            ss << p->name << "\t" << format_timestamp_for_display(p->creation_time) << "\t"
               << "Core: " << p->core_id << "\t"
               << p->instructions_executed << " / " << p->instructions_total;
            
            if (current_scheduler_type == RR) {
                ss << "\tQuantum Left: " << p->remaining_quantum;
            }
            ss << endl;
            anyRunning = true;
        }
    }
    if (!anyRunning) ss << "No running processes\n";

    ss << "\n==== FINISHED PROCESSES ====\n";
    bool anyFinished = false;
    for (const auto& p : g_finished_processes) {
        ss << p->name << "\t" << format_timestamp_for_display(p->creation_time) << "\t"
           << "Finished\t"
           << p->instructions_executed << " / " << p->instructions_total << endl;
        anyFinished = true;
    }
    if (!anyFinished) ss << "No finished processes\n";

    // Add cancelled processes section
    {
        lock_guard<mutex> lock(g_cancelled_processes_mutex);
        if (!g_cancelled_processes.empty()) {
            ss << "\n==== CANCELLED PROCESSES ====\n";
            for (const auto& cp : g_cancelled_processes) {
                ss << cp.process->name << "\t(" << cp.timestamp << ")\t"
                   << "Finished\t"
                   << cp.process->instructions_executed << " / " << cp.process->instructions_total << endl;
            }
        }
    }

    return ss.str();
}

string getVMStatReport() {
    lock_guard<mutex> lock_process(g_process_lists_mutex);
    lock_guard<mutex> lock_paging(g_paging_mutex);
    stringstream ss;

    ss << "==== DETAILED VIEW ====\n";
    
    // Active/Inactive Processes
    int active_processes = 0;
    int inactive_processes = 0;
    int ready_processes = 0;
    
    // Count running processes
    for (int i = 0; i < config_num_cpu; ++i) {
        if (g_running_processes[i] != nullptr) {
            active_processes++;
        }
    }
    
    // Count ready processes
    {
        lock_guard<mutex> ready_lock(g_ready_queue_mutex);
        queue<PCB*> temp_queue = g_ready_queue;
        while (!temp_queue.empty()) {
            ready_processes++;
            temp_queue.pop();
        }
    }
    
    // Count finished processes (inactive)
    inactive_processes = g_finished_processes.size();
    
    ss << "Active processes: " << active_processes << "\n";
    ss << "Inactive processes: " << inactive_processes << "\n";
    ss << "Ready processes: " << ready_processes << "\n\n";
    
    // Available/Used Memory
    int total_memory = g_max_overall_mem;
    int used_memory = 0;
    int free_memory = 0;
    
    // Count used and free frames
    int used_frames = 0;
    int free_frames = 0;
    
    for (int i = 0; i < g_total_frames; i++) {
        if (g_physical_frames[i].is_free) {
            free_frames++;
        } else {
            used_frames++;
        }
    }
    
    used_memory = used_frames * g_mem_per_frame;
    free_memory = free_frames * g_mem_per_frame;
    
    ss << "Total memory: " << total_memory << " bytes\n";
    ss << "Used memory: " << used_memory << " bytes\n";
    ss << "Free memory: " << free_memory << " bytes\n\n";
    
    // Ticks
    unsigned long long total_ticks = g_total_cpu_ticks.load();
    unsigned long long idle_ticks = g_idle_cpu_ticks.load();
    unsigned long long active_ticks = g_active_cpu_ticks.load();
    
    ss << "Idle cpu ticks: " << idle_ticks << "\n";
    ss << "Active cpu ticks: " << active_ticks << "\n";
    ss << "Total cpu ticks: " << total_ticks << "\n\n";
    
    // Pages
    int paged_in = g_pages_paged_in.load();
    int paged_out = g_pages_paged_out.load();
    
    ss << "Num paged in: " << paged_in << "\n";
    ss << "Num paged out: " << paged_out << "\n";
    
    return ss.str();
}