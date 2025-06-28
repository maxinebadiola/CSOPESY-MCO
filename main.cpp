#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable> // Added missing header
#include <chrono>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <memory> 
#include <cstdlib> 
#include <map>
#include <unordered_map>
#include <random>

using namespace std;

enum ProcessState {
    READY,
    RUNNING,
    FINISHED
};

enum SchedulerType {
    FCFS,
    RR
};

struct PCB {
    int id;
    string name;
    ProcessState state;
    time_t creation_time;
    int instructions_total;
    atomic<int> instructions_executed; 
    string output_filename;
    int core_id; 
    int remaining_quantum;

    vector<string> logs; 

    PCB(int p_id, const string& p_name, ProcessState p_state, time_t p_creation_time, 
        int p_instr_total, int p_instr_exec, const string& p_filename, int p_core_id)
      : id(p_id), name(p_name), state(p_state), creation_time(p_creation_time),
        instructions_total(p_instr_total), instructions_executed(p_instr_exec), 
        output_filename(p_filename), core_id(p_core_id), remaining_quantum(0), logs() {}
};

queue<PCB*> g_ready_queue;
mutex g_ready_queue_mutex;

vector<PCB*> g_running_processes; // Size will be set from config file
vector<PCB*> g_finished_processes;
mutex g_process_lists_mutex;

atomic<bool> g_exit_flag(false);
vector<unique_ptr<PCB>> g_process_storage;

thread g_scheduler_thread;
vector<thread> g_worker_threads;
atomic<bool> g_threads_started(false);

const int TICK_DURATION_MS = 10; // Duration of one CPU tick in milliseconds
atomic<unsigned long long> g_cpu_ticks(0);
mutex g_tick_mutex;
condition_variable g_tick_cv;
thread g_tick_thread;

class Console; //forward declaration for W6 variables [DO NOT REMOVE]

//W6: variables for FCFS scheduler-start (old globals removed, replaced by above)
mutex outputMutex; //sync console for logging
map<string, Console> screens; 

//config.txt variables
int config_num_cpu;
string config_scheduler;
int config_quantum_cycles;
int config_batch_process_freq;
int config_min_ins;
int config_max_ins;
int config_delay_per_exec;
//for basic program instructions (DECLARE, ADD, SUBTRACT, PRINT etc.)
std::unordered_map<std::string, uint16_t> variables;
std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<uint16_t> dist(0, 65535);
//if these instructions can be used in a process
bool enable_sleep = false;
bool enable_for = false;

SchedulerType current_scheduler_type;

void printConfigVars();
void readConfigFile();

void printHeader() {
    cout << R"(
   _____  _____  ____  _____  ______  _______     __
  / ____|/ ____|/ __ \|  __ \|  ____|/ ____\ \   / /
 | |    | (___ | |  | | |__) | |__  | (___  \ \_/ / 
 | |     \___ \| |  | |  ___/|  __|  \___ \  \   /  
 | |____ ____) | |__| | |    | |____ ____) |  | |   
  \_____|_____/ \____/|_|    |______|_____/   |_|   

                  CSOPESY CLI EMULATOR
)" << endl;
}

void printInitial()
{
   cout << "1. initialize" << endl;
   cout << "2. exit" << endl;
   cout << "3. enable SLEEP" << endl;
   cout << "4. enable FOR" << endl;
}

void printMenuCommands() {
   cout << "==== MAIN MENU ====" << endl;
   cout << "Available Commands:" << endl;
   cout << "1. screen -s <name>" << endl;
   cout << "2. screen -r <name>" << endl;
   cout << "3. screen -ls" << endl; //cpu utilization report, list of processes
   cout << "4. clear / cls" << endl;
   cout << "5. exit" << endl;
}

void printScreenCommands() {
cout << "==== SCREEN COMMANDS ====" << endl;
cout << "1. scheduler-start" << endl; //continously generate basic process instructions
cout << "2. scheduler-stop" << endl; //stops generating processes
cout << "3. screen -ls" << endl; //cpu utilization report, list of processes
cout << "4. report-util" << endl; //same as screen -ls but exports to csopesy-log.txt
cout << "5. process-smi" << endl; //simple information about the process
cout << "6. screen" << endl; //not really needed for MO1 tbh
cout << "7. clear / cls" << endl; 
cout << "8. exit" << endl;
}

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

//COMMENT OUT THE FUNC BELOW FOR W6 SUBMISSION (will slow down program)
// void initializeLogs(PCB* process) {
//     if (process->output_filename.empty()) return;
//     ofstream outFile(process->output_filename);
//     if (outFile.is_open()) {
//         outFile << "Process name: " << process->name << endl;
//         outFile << "Logs:" << endl;
//     }
// }
// void exportLogs(PCB* process, int coreId, const string& message) {
//     if (process->output_filename.empty()) return;
//     lock_guard<mutex> lock(outputMutex);
//     ofstream outFile(process->output_filename, ios::app);
//     if (outFile.is_open()) {
//         outFile << getCurrentTimestampWithMillis() << " Core:" << coreId << " \"" << message << "\"" << endl;
//     }
// }

void tick_generator_thread() {
    while (!g_exit_flag) {
        // Sleep for the configured duration of one tick
        this_thread::sleep_for(chrono::milliseconds(TICK_DURATION_MS));
        
        // Lock, increment the global tick counter
        {
            lock_guard<mutex> lock(g_tick_mutex);
            g_cpu_ticks++;
        }
        
        // Notify all waiting worker threads that a new tick has occurred
        g_tick_cv.notify_all();
    }
}

//BASIC PROGRAM INSTRUCTIONS
//change variable value 
void DECLARE(const std::string& var, uint16_t value) {
    variables[var] = value;
}

//get value of variable or convert string to uint16
uint16_t getValue(const std::string& varOrValue) {
    if (variables.count(varOrValue)) return variables[varOrValue];
    try {
        return static_cast<uint16_t>(std::stoi(varOrValue));
    } catch (const std::exception&) {
        return 0; 
    }
}


//ADD: var1 = op2 + op3 (op2/op3 can be variable or value)
void ADD(const std::string& var1, const std::string& op2, const std::string& op3) {
    if (!variables.count(var1)) variables[var1] = 0;
    uint16_t val2 = getValue(op2);
    uint16_t val3 = getValue(op3);
    uint32_t sum = static_cast<uint32_t>(val2) + static_cast<uint32_t>(val3);
    if (sum > 65535) sum = 65535;
    variables[var1] = static_cast<uint16_t>(sum);
}

//SUBTRACT: var1 = op2 - op3 (op2/op3 can be variable or value)
void SUBTRACT(const std::string& var1, const std::string& op2, const std::string& op3) {
    if (!variables.count(var1)) variables[var1] = 0;
    uint16_t val2 = getValue(op2);
    uint16_t val3 = getValue(op3);
    int32_t diff = static_cast<int32_t>(val2) - static_cast<int32_t>(val3);
    if (diff < 0) diff = 0;
    variables[var1] = static_cast<uint16_t>(diff);
}

//set var1/2/3 to default = 0 
double setVariableDefault() {
    for (auto& kv : variables) kv.second = 0;
    return 0;
}

//pick random variable (1/2/3)
std::string randomVariable() {
    static const std::vector<std::string> vars = {"var1", "var2", "var3"};
    std::uniform_int_distribution<int> pick(0, 2);
    return vars[pick(gen)];
}

//pick random uint16 value
std::string randomUint16Value() {
    return std::to_string(dist(gen));
}

//pick var1/2/3 or random uint16 value
std::string randomVarOrValue() {
    if (dist(gen) % 2) return randomVariable();
    return randomUint16Value();
}

// Print a message, always showing Value of (random variable) = (value), Default print msg is "Hello World! from <screen/process name>"
void PRINT(const std::string& msg, PCB* current_process = nullptr, const std::string& process_name = "", const std::string& screen_name = "") {
    std::string output = msg;
    
    if (msg.empty() && !process_name.empty()) {
        output = "Hello world from " + process_name + "!";
    }
    
    for (const auto& var : variables) {
        std::string varName = var.first;
        std::string varValue = std::to_string(var.second);
        
        size_t pos = 0;
        while ((pos = output.find(varName, pos)) != std::string::npos) {
            bool isWholeVar = true;
            if (pos > 0 && (std::isalnum(output[pos - 1]) || output[pos - 1] == '_')) {
                isWholeVar = false;
            }
            if (pos + varName.length() < output.length() && 
                (std::isalnum(output[pos + varName.length()]) || output[pos + varName.length()] == '_')) {
                isWholeVar = false;
            }
            
            if (isWholeVar) {
                output.replace(pos, varName.length(), varValue);
                pos += varValue.length();
            } else {
                pos += varName.length();
            }
        }
    }
    
    if (current_process) {
        // Store in process logs instead of printing
        std::lock_guard<std::mutex> lock(outputMutex);
        current_process->logs.push_back(output);
    } else {
        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << output << std::endl;
    }
}

// SLEEP(X) - sleeps the current process for X (uint8) CPU ticks and relinquishes the CPU. 
void SLEEP(uint8_t ticks) {
    if (ticks == 0) return;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(config_delay_per_exec * ticks));
}

void executeInstructionSet(const std::vector<std::string>& instructions, int nestingLevel, PCB* current_process);

// FOR(instruction, val)
void FOR(const std::vector<std::string>& instructions, int repeats, int nestingLevel, PCB* current_process) {
    if (nestingLevel >= 3) {
        std::lock_guard<std::mutex> lock(outputMutex);
        std::cout << "Maximum nesting level (3) reached. Skipping nested FOR loop." << std::endl;
        return;
    }

    if (repeats < 0) repeats = 0;
    if (repeats > 100) repeats = 100; 
    
    for (int i = 0; i < repeats; ++i) {
        executeInstructionSet(instructions, nestingLevel + 1, current_process);
        
        if (g_exit_flag) break;
    }
}

void executeInstructionSet(const std::vector<std::string>& instructions, int nestingLevel, PCB* current_process) {
    for (const std::string& instruction : instructions) {
        if (g_exit_flag) break; 
        
        std::istringstream iss(instruction);
        std::string command;
        iss >> command;
        
        if (command == "DECLARE") {
            std::string var;
            std::string valueStr;
            if (iss >> var >> valueStr) {
                try {
                    uint16_t value = static_cast<uint16_t>(std::stoi(valueStr));
                    DECLARE(var, value);
                } catch (const std::exception&) {
                    DECLARE(var, 0); // Default to 0 if parsing fails
                }
            }
        }
        else if (command == "ADD") {
            std::string var1, op2, op3;
            if (iss >> var1 >> op2 >> op3) {
                ADD(var1, op2, op3);
            }
        }
        else if (command == "SUBTRACT") {
            std::string var1, op2, op3;
            if (iss >> var1 >> op2 >> op3) {
                SUBTRACT(var1, op2, op3);
            }
        }
        else if (command == "PRINT") {
            std::string msg;
            std::getline(iss, msg);

            if (!msg.empty()) {
                msg.erase(0, msg.find_first_not_of(" \t"));
                if (msg.length() >= 2 && msg.front() == '"' && msg.back() == '"') {
                    msg = msg.substr(1, msg.length() - 2);
                }
            }
            PRINT(msg, current_process);
        }
        else if (command == "SLEEP") {
            std::string ticksStr;
            if (iss >> ticksStr) {
                try {
                    uint8_t ticks = static_cast<uint8_t>(std::stoi(ticksStr));
                    SLEEP(ticks);
                } catch (const std::exception&) {
                    // Skip invalid SLEEP commands
                }
            }
        }
        else if (command == "FOR") {
            std::string line;
            std::getline(iss, line);
            
            // Parse FOR command more carefully
            size_t lastSpace = line.find_last_of(' ');
            if (lastSpace != std::string::npos) {
                std::string instructionsStr = line.substr(0, lastSpace);
                std::string repeatsStr = line.substr(lastSpace + 1);
                
                instructionsStr.erase(0, instructionsStr.find_first_not_of(" \t"));
                instructionsStr.erase(instructionsStr.find_last_not_of(" \t") + 1);
                
                int repeats = 0;
                try {
                    repeats = std::stoi(repeatsStr);
                } catch (const std::exception&) {
                    repeats = 0;
                }
                
                std::vector<std::string> forInstructions;
                std::istringstream instrStream(instructionsStr);
                std::string singleInstr;
                
                while (std::getline(instrStream, singleInstr, ',')) {
                    singleInstr.erase(0, singleInstr.find_first_not_of(" \t"));
                    singleInstr.erase(singleInstr.find_last_not_of(" \t") + 1);
                    if (!singleInstr.empty()) {
                        forInstructions.push_back(singleInstr);
                    }
                }
                
                FOR(forInstructions, repeats, nestingLevel, current_process);
            }
        }
    }
}

std::vector<std::string> generateRandomInstructions(const std::string& processName, int count, bool enable_sleep, bool enable_for) {
    std::vector<std::string> instructions;

    // Build list of possible instruction types
    std::vector<int> possibleInstructions = {0, 1, 2, 3, 4}; // 0: DECLARE, 1: ADD, 2: SUBTRACT, 3: PRINT var, 4: PRINT default
    if (enable_sleep) possibleInstructions.push_back(5);     // 5: SLEEP
    if (enable_for) possibleInstructions.push_back(6);       // 6: FOR

    std::uniform_int_distribution<int> instrType(0, possibleInstructions.size() - 1);

    for (int i = 0; i < count; ++i) {
        switch (possibleInstructions[instrType(gen)]) {
            case 0: // DECLARE
                instructions.push_back("DECLARE " + randomVariable() + " " + randomUint16Value());
                break;
            case 1: // ADD
                instructions.push_back("ADD " + randomVariable() + " " + randomVarOrValue() + " " + randomVarOrValue());
                break;
            case 2: // SUBTRACT
                instructions.push_back("SUBTRACT " + randomVariable() + " " + randomVarOrValue() + " " + randomVarOrValue());
                break;
            case 3: // PRINT with variable
                {
                    std::string var = randomVariable();
                    instructions.push_back("PRINT \"Value of " + var + " is " + var + "\"");
                }
                break;
            case 4: // PRINT default message
                instructions.push_back("PRINT \"Hello world from " + processName + "!\"");
                break;
            case 5: // SLEEP
                instructions.push_back("SLEEP " + std::to_string(rand() % 1000)); // random sleep ms
                break;
            case 6: // FOR loop placeholder
                instructions.push_back("FOR " + std::to_string(rand() % 5 + 1)); // random repeat count 1-5
                break;
        }
    }

    return instructions;
}


//print current values of var1, var2, var3
void printVarValues() {
    std::cout << "Values of\n";
    std::cout << "var1 = " << variables["var1"] << std::endl;
    std::cout << "var2 = " << variables["var2"] << std::endl;
    std::cout << "var3 = " << variables["var3"] << std::endl;
}

// FCFS
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
            // Initialize process-specific variables (thread-local)
            variables.clear();
            variables["var1"] = 0;
            variables["var2"] = 0;
            variables["var3"] = 0;

            while (current_process->instructions_executed < current_process->instructions_total && !g_exit_flag) {
                // Wait for CPU ticks instead of sleep
                for (int tick_count = 0; tick_count < config_delay_per_exec; ++tick_count) {
                    if (g_exit_flag) break;

                    unsigned long long last_known_tick = g_cpu_ticks.load();
                    unique_lock<mutex> lock(g_tick_mutex);
                    g_tick_cv.wait(lock, [&]{
                        return g_cpu_ticks.load() > last_known_tick || g_exit_flag.load();
                    });
                }
                if (g_exit_flag) break;

                // Generate and execute one random instruction
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


// Round Robin
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
            // Initialize quantum if new process assignment
            if (current_process->remaining_quantum <= 0) {
                current_process->remaining_quantum = config_quantum_cycles;
                // Initialize process-specific variables (thread-local)
                variables.clear();
                variables["var1"] = 0;
                variables["var2"] = 0;
                variables["var3"] = 0;
            }

            if (current_process->instructions_executed < current_process->instructions_total && !g_exit_flag) {
                // Wait for CPU ticks instead of sleep
                for (int tick_count = 0; tick_count < config_delay_per_exec; ++tick_count) {
                    if (g_exit_flag) break;

                    unsigned long long last_known_tick = g_cpu_ticks.load();
                    unique_lock<mutex> lock(g_tick_mutex);
                    g_tick_cv.wait(lock, [&]{
                        return g_cpu_ticks.load() > last_known_tick || g_exit_flag.load();
                    });
                }
                if (g_exit_flag) break;

                // Generate and execute one random instruction
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

            // Check if process finished or quantum expired
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

                    // Add back to ready queue
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

void stopAndResetScheduler() {
    g_exit_flag = true;

    g_tick_cv.notify_all();

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
    g_exit_flag = false;
    g_threads_started = false;

    cout << "Scheduler stopped and reset successfully." << endl;
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
                            
                            if (current_scheduler_type == RR) { // Reset quantum for RR
                                process_to_schedule->remaining_quantum = config_quantum_cycles;
                            }
                            
                            g_running_processes[i] = process_to_schedule;
                            scheduled = true;
                            break;
                        }
                    }
                }
                
                if (!scheduled) { // All cores busy
                    this_thread::sleep_for(chrono::milliseconds(50));
                }
            }
        } else { // No processes in ready queue
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void clearScreen() {
   #if defined(_WIN64)
       system("cls");
   #else
       system("clear");
   #endif
   printHeader();
}
   
class Console {
public:
    string name;
    int currentLine;
    int totalLines;
    string timestamp;
    Console() : name(""), currentLine(0), totalLines(0), timestamp("") {}
    Console(const string& name, int total) {
        this->name = name;
        currentLine = 0;
        totalLines = total;
        time_t now = time(0);
        tm* localTime = localtime(&now); 
        char buffer[100];
        strftime(buffer, sizeof(buffer), "%m/%d/%Y, %I:%M:%S %p", localTime);
        timestamp = buffer;
    }
    void displayInfo() {
        cout << "Process Name: " << name << endl;
        cout << "Current Line: " << currentLine << " / " << totalLines << endl;
        cout << "Created At: " << timestamp << endl;
    }
};

string getSystemReport() {
    lock_guard<mutex> lock(g_process_lists_mutex);
    stringstream ss;

    ss << "==== CPU UTILIZATION REPORT ====\n";
    int used_cores = 0;
    for (int i = 0; i < config_num_cpu; ++i) {
        if (g_running_processes[i] != nullptr) used_cores++;
    }

    // CPU Utilization
    double cpu_utilization = (static_cast<double>(used_cores) / config_num_cpu) * 100.0;
    ss << fixed << setprecision(1); // show one decimal place
    ss << "CPU Utilization: " << cpu_utilization << "%\n";
    ss << "Current CPU Tick: " << g_cpu_ticks.load() << "\n"; 
    ss << "Cores Used: " << used_cores << endl;
    ss << "Cores available: " << (config_num_cpu - used_cores) << endl;
    ss << "Scheduler: " << (current_scheduler_type == FCFS ? "First-Come-First-Served (FCFS)" : "Round Robin (RR)");
    if (current_scheduler_type == RR) {
        ss << " [Quantum: " << config_quantum_cycles << " cycles]";
    }
    ss << endl;

    int ready_count = 0; // Ready queue size
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

// Rewritten to use config values and new PCB structure
void createTestProcesses(const string& screenName) {
    lock_guard<mutex> lock(g_ready_queue_mutex);
    
    int current_process_count = g_process_storage.size();
    
    // Use config_batch_process_freq to determine how many processes to create
    for (int i = 0; i < config_batch_process_freq; ++i) {
        int process_id = current_process_count + i;
        string processName = screenName + "_" + (process_id + 1 < 10 ? "0" : "") + to_string(process_id + 1);
        string filename = "screen_" + processName + ".txt";

        // Use config values for instruction count
        int instructions = rand() % (config_max_ins - config_min_ins + 1) + config_min_ins;

        g_process_storage.push_back(make_unique<PCB>(
            process_id, processName, READY, time(0), instructions, 0, filename, -1
        ));

        g_ready_queue.push(g_process_storage.back().get());
    }
}

//
//W3 NEW: screen handling
void screenSession(Console& screen) {
    clearScreen(); 
    cout << "==== SCREEN SESSION: " << screen.name << " ====" << endl;
    screen.displayInfo();
    printScreenCommands();

    while (true) {
        cout << "\n" << screen.name << " > ";
        string screenCmd;
        getline(cin, screenCmd);
        if (screenCmd == "exit") {
            screen.currentLine++;
            clearScreen();
            printMenuCommands();
            cout << "\nExiting screen session..." << endl;
            break; 
        } else if (screenCmd == "clear" || screenCmd == "cls") {
            screen.currentLine++;
            clearScreen();
            cout << "==== SCREEN SESSION: " << screen.name << " ====" << endl;
            screen.displayInfo();
            printScreenCommands();
            continue;
        } else if (screenCmd == "scheduler-start") {
            if (!g_threads_started) {
                cout << "Starting " << config_scheduler << " scheduler with " << config_num_cpu << " CPU cores..." << endl;
                
                g_tick_thread = thread(tick_generator_thread);//CPU itegration
                g_scheduler_thread = thread(schedulerThread);
                
                for (int i = 0; i < config_num_cpu; ++i) {
                    if (current_scheduler_type == FCFS) {
                        g_worker_threads.emplace_back(fcfs_worker_thread, i);
                    } else {
                        g_worker_threads.emplace_back(rr_worker_thread, i);
                    }
                }
                g_threads_started = true;
            }
            createTestProcesses(screen.name);
            cout << "Added " << config_batch_process_freq << " new processes to the scheduling queue." << endl;
            screen.currentLine++;
        } 
        else if (screenCmd == "scheduler-stop") {
            cout << "Stopping and resetting the scheduler..." << endl;
            stopAndResetScheduler(); // Our new clean-and-reset function
            screen.currentLine++;
        } else if (screenCmd == "report-util") {
            screen.currentLine++;

            string report = getSystemReport();

            // Print to console
            cout << report;

            // Export to file
            ofstream outFile("csopesy-log.txt", ios::app); // append mode
            if (outFile.is_open()) {
                outFile << "=== SYSTEM REPORT SAVED AT " << getCurrentTimestampWithMillis() << " ===\n";
                outFile << report << endl;
                outFile.close();
                cout << "Report saved to csopesy-log.txt" << endl;
            } else {
                cout << "Failed to save report to file." << endl;
            }
        } else if (screenCmd == "screen") {
            screen.currentLine++;
            screen.displayInfo();
        } else if (screenCmd == "screen -ls") {
            screen.currentLine++;
            string report = getSystemReport();
            cout << report;
        }
        else if (screenCmd == "process-smi") {
            screen.currentLine++;

            PCB* currentProcess = nullptr;
            {
                lock_guard<mutex> lock(g_process_lists_mutex);
                for (int i = 0; i < config_num_cpu; ++i) {
                    if (g_running_processes[i] && g_running_processes[i]->name.find(screen.name) != string::npos) {
                        currentProcess = g_running_processes[i];
                        break;
                    }
                }
                if (!currentProcess) {
                    for (const auto& p : g_finished_processes) {
                        if (p->name.find(screen.name) != string::npos) {
                            currentProcess = p;
                            break;
                        }
                    }
                }
            }

            if (currentProcess) {
                cout << "\n==== PROCESS-SMI ====" << endl;
                cout << "Name: " << currentProcess->name << endl;
                cout << "ID: " << currentProcess->id << endl;
                cout << "State: ";
                switch(currentProcess->state) {
                    case READY: cout << "READY"; break;
                    case RUNNING: cout << "RUNNING"; break;
                    case FINISHED: cout << "FINISHED"; break;
                }
                cout << endl;

                cout << "Created At: " << format_timestamp_for_display(currentProcess->creation_time) << endl;
                cout << "Instructions: " << currentProcess->instructions_executed.load() 
                    << " / " << currentProcess->instructions_total << endl;

                if (currentProcess->state == FINISHED) {
                    cout << "Status: Finished!" << endl;
                }

                cout << "\n==== LOGS ====" << endl;
                if (!currentProcess->logs.empty()) {
                    for (const auto& log : currentProcess->logs) {
                        cout << log << endl;
                    }
                } else {
                    cout << "No PRINT logs recorded yet." << endl;
                }
            } else {
                cout << "No process found associated with this screen." << endl;
            }
        }
        else {
            cout << "Unrecognized command. Please try again." << endl;
            screen.currentLine++;
        }
    }
}

//W3 NEW: main menu
void menuSession() {
    bool initialized = false;
    bool firstLoad = true;
    // Only print header and initial menu once
    while (true) {
        if (!initialized && firstLoad) {
            printHeader();
            printInitial();
            firstLoad = false;
        }
        cout << "\n> ";
        string command;
        getline(cin, command);

        if (!initialized) {
            if (command == "initialize") {
                initialized = true;
                readConfigFile();
                g_running_processes.assign(config_num_cpu, nullptr);
                clearScreen();
                printMenuCommands();
                printConfigVars();
                continue;
            } else if (command == "exit") {
                cout << "Exiting program..." << endl;
                break;
            }  else if (command == "enable SLEEP") {
                enable_sleep = true;
                cout << "SLEEP enabled." << endl;
                continue;
            } else if (command == "enable FOR") {
                enable_for = true;
                cout << "FOR enabled." << endl;
                continue;
            }
            else {
                cout << "Please type 'initialize' to start or 'exit' to quit." << endl;
                continue;
            }
        }

        if (command == "exit") {
            cout << "Exiting program..." << endl;
            break;
        } else if (command == "clear" || command == "cls") {
            clearScreen();
            printMenuCommands();
            continue;
        } else if (command.find("screen -s ") == 0) {
            string name = command.substr(10);
            if (name.empty()) {
                cout << "Please provide a name for the screen session." << endl;
            } else if (screens.find(name) != screens.end()) {
                cout << "Screen session already exists: " << name << endl;
                cout << "Created At: " << screens[name].timestamp << endl;
            } else {
                Console newScreen(name, 100);
                screens[name] = newScreen;
                cout << "New screen session created: " << name << endl;
                screenSession(screens[name]);
                clearScreen();
                printMenuCommands();
            }
        } else if (command.find("screen -r ") == 0) {
            string name = command.substr(10);
            if (name.empty()) {
                cout << "Please provide a name to resume a screen session." << endl;
            } else if (screens.find(name) == screens.end()) {
                cout << "Screen session not found: " << name << endl;
            } else {
                screenSession(screens[name]);
                clearScreen();
                printMenuCommands();
            } 
        } else if (command == "screen -ls") {
            string report = getSystemReport();
            cout << report;
        } else {
            cout << "Unrecognized command. Please try again." << endl;
        }
    }
}

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
                current_scheduler_type = FCFS; // default to FCFS
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
        } else {
            string skip;
            configFile >> skip;
        }
    }
    configFile.close();
}

//FOR TESTING
// === MODIFIED ===
void printConfigVars() {
    cout << "\n[CONFIG VALUES LOADED]" << endl;
    cout << "num-cpu: " << config_num_cpu << endl;
    cout << "scheduler: " << config_scheduler << " (" << (current_scheduler_type == FCFS ? "FCFS" : "Round Robin") << ")" << endl;
    cout << "quantum-cycles: " << config_quantum_cycles << endl;
    cout << "batch-process-freq: " << config_batch_process_freq << endl;
    cout << "min-ins: " << config_min_ins << endl;
    cout << "max-ins: " << config_max_ins << endl;
    cout << "delay-per-exec: " << config_delay_per_exec << " ticks" << endl;
    cout << "[System Info] Tick Duration: " << TICK_DURATION_MS << " ms" << endl;
}
// === END MODIFIED ===

int main() {
    srand(time(0)); // Seed random number generator
    menuSession();
    return 0;
}