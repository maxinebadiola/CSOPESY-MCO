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
    stringstream ss;
    ss << put_time(&localTime, "(%m/%d/%Y | %I:%M:%S");
    ss << '.' << setfill('0') << setw(3) << value.count() % 1000;
    ss << (localTime.tm_hour >= 12 ? " PM)" : " AM)");
    return ss.str();
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

    return ss.str();
}