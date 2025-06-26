#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <memory> 
#include <cstdlib> 
#include <map>

using namespace std;

enum ProcessState {
    READY,
    RUNNING,
    FINISHED
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

    PCB(int p_id, const string& p_name, ProcessState p_state, time_t p_creation_time, 
        int p_instr_total, int p_instr_exec, const string& p_filename, int p_core_id)
      : id(p_id), name(p_name), state(p_state), creation_time(p_creation_time),
        instructions_total(p_instr_total), instructions_executed(p_instr_exec), 
        output_filename(p_filename), core_id(p_core_id) {}
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
}

void printMenuCommands() {
   cout << "==== MAIN MENU ====" << endl;
   cout << "Available Commands:" << endl;
   cout << "1. screen -s <name>" << endl;
   cout << "2. screen -r <name>" << endl;
   cout << "3. clear / cls" << endl;
   cout << "4. exit" << endl;
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
void initializeLogs(PCB* process) {
    if (process->output_filename.empty()) return;
    ofstream outFile(process->output_filename);
    if (outFile.is_open()) {
        outFile << "Process name: " << process->name << endl;
        outFile << "Logs:" << endl;
    }
}
//COMMENT OUT THE FUNC BELOW FOR W6 SUBMISSION (will slow down program)
void exportLogs(PCB* process, int coreId, const string& message) {
    if (process->output_filename.empty()) return;
    lock_guard<mutex> lock(outputMutex);
    ofstream outFile(process->output_filename, ios::app);
    if (outFile.is_open()) {
        outFile << getCurrentTimestampWithMillis() << " Core:" << coreId << " \"" << message << "\"" << endl;
    }
}

// New FCFS Core Logic
void worker_thread(int core_id) {
    while (!g_exit_flag) {
        PCB* current_process = nullptr;
        {
            lock_guard<mutex> lock(g_process_lists_mutex);
            if (core_id < g_running_processes.size()) {
                current_process = g_running_processes[core_id];
            }
        }

        if (current_process != nullptr) {
            //initializeLogs(current_process);
            
            while (current_process->instructions_executed < current_process->instructions_total) {
                if (g_exit_flag) break;
                
                // Use delay from config file
                this_thread::sleep_for(chrono::milliseconds(config_delay_per_exec));
                
                //stringstream msg;
                //msg << "Executing instruction for " << current_process->name << "! (" 
                //    << (current_process->instructions_executed.load() + 1) << "/" 
                //    << current_process->instructions_total << ")";
                //exportLogs(current_process, core_id, msg.str());

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
            bool scheduled = false;
            while (!scheduled && !g_exit_flag) {
                {
                    lock_guard<mutex> lock(g_process_lists_mutex);
                    for (int i = 0; i < config_num_cpu; ++i) { // Use config_num_cpu
                        if (g_running_processes[i] == nullptr) {
                            process_to_schedule->state = RUNNING;
                            process_to_schedule->core_id = i;
                            g_running_processes[i] = process_to_schedule;
                            scheduled = true;
                            break;
                        }
                    }
                }
                if (!scheduled) {
                    this_thread::sleep_for(chrono::milliseconds(100));
                }
            }
        } else {
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
    ss << "Total Cores: " << config_num_cpu << endl;
    int used_cores = 0;
    for (int i = 0; i < config_num_cpu; ++i) {
        if (g_running_processes[i] != nullptr) used_cores++;
    }
    ss << "Used Cores: " << used_cores << endl;
    ss << "Available Cores: " << (config_num_cpu - used_cores) << endl;

    ss << "\n==== RUNNING PROCESSES ====\n";
    bool anyRunning = false;
    for (int i = 0; i < config_num_cpu; ++i) {
        PCB* p = g_running_processes[i];
        if (p != nullptr) {
            ss << p->name << "\t" << format_timestamp_for_display(p->creation_time) << "\t"
               << "Core: " << p->core_id << "\t"
               << p->instructions_executed << " / " << p->instructions_total << endl;
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
                cout << "Starting scheduler and " << config_num_cpu << " CPU cores for the first time..." << endl;
                g_scheduler_thread = thread(schedulerThread);
                for (int i = 0; i < config_num_cpu; ++i) {
                    g_worker_threads.emplace_back(worker_thread, i);
                }
                g_threads_started = true;
            }
            createTestProcesses(screen.name);
            cout << "Added " << config_batch_process_freq << " new processes to the scheduling queue." << endl;
            screen.currentLine++;
        } 
        else if (screenCmd == "scheduler-stop") {
            cout << "Scheduler-stop command recognized. (Note: use 'exit' in main menu to stop all processes)" << endl;
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

                ifstream inFile(currentProcess->output_filename);
                if (inFile.is_open()) {
                    string line;
                    cout << "\n==== LOGS ====" << endl;
                    while (getline(inFile, line)) {
                        cout << line << endl;
                    }
                    inFile.close();
                } else {
                    cout << "No logs available yet." << endl;
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
            } else {
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
            configFile >> ws;
            getline(configFile, sched, '"'); // skip first quote
            getline(configFile, sched, '"'); // get value inside quotes
            config_scheduler = sched;
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
}
//FOR TESTING
void printConfigVars() {
    cout << "\n[CONFIG VALUES LOADED]" << endl;
    cout << "num-cpu: " << config_num_cpu << endl;
    cout << "scheduler: " << config_scheduler << endl;
    cout << "quantum-cycles: " << config_quantum_cycles << endl;
    cout << "batch-process-freq: " << config_batch_process_freq << endl;
    cout << "min-ins: " << config_min_ins << endl;
    cout << "max-ins: " << config_max_ins << endl;
    cout << "delay-per-exec: " << config_delay_per_exec << endl;
}

int main() {
    srand(time(0)); // Seed random number generator
    menuSession();
    return 0;
}