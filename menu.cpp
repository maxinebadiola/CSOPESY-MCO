// menu.cpp
#include "headers.h"
#include <iomanip>

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

void printInitial() {
   cout << "1. initialize" << endl;
   cout << "2. exit" << endl;
   cout << "3. enable SLEEP" << endl;
   cout << "4. enable FOR" << endl;
}

void printMenuCommands() {
    cout << "==== MAIN MENU ====" << endl;
    cout << "Available Commands:" << endl;
    cout << "1. screen -s <name> <process_memory_size>" << endl;
    cout << "2. screen -r <name>" << endl;
    cout << "3. screen -ls" << endl;
    cout << "4. report-util" << endl;
    cout << "5. clear / cls" << endl;
    cout << "6. exit" << endl;
}

void printScreenCommands() {
    cout << "==== SCREEN COMMANDS ====" << endl;
    cout << "1. scheduler-start" << endl;
    cout << "2. scheduler-stop" << endl;
    cout << "3. process-smi" << endl;
    cout << "4. vmstat" << endl;
    cout << "5. screen -ls" << endl;
    cout << "6. screen" << endl;
    cout << "7. clear / cls" << endl; 
    cout << "8. exit" << endl;
}

Console::Console(const string& name, int total) {
    this->name = name;
    currentLine = 0;
    totalLines = total;
    time_t now = time(0);
    tm* localTime = localtime(&now); 
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%m/%d/%Y, %I:%M:%S %p", localTime);
    timestamp = buffer;
}

void Console::displayInfo() {
    cout << "Process Name: " << name << endl;
    cout << "Current Line: " << currentLine << " / " << totalLines << endl;
    cout << "Created At: " << timestamp << endl;
}

void clearScreen() {
   #if defined(_WIN64)
       system("cls");
   #else
       system("clear");
   #endif
   printHeader();
}

void printVmstat() {
    lock_guard<mutex> lock(g_memory_mutex);

    int total_memory = g_max_overall_mem;
    int used_memory = 0;
    int free_memory = 0;
    
    for (const auto& block : g_memory_blocks) {
        if (block.is_free) {
            free_memory += block.size;
        } else {
            used_memory += block.size;
        }
    }

    long long idle_ticks = g_idle_cpu_ticks.load();
    long long active_ticks = g_active_cpu_ticks.load();
    long long total_ticks = idle_ticks + active_ticks;

    printf("\n");
    printf("      %d K total memory\n", total_memory / 1024);
    printf("      %d K used memory\n", used_memory / 1024);
    printf("      %d K free memory\n", free_memory / 1024);
    printf("      %lld idle cpu ticks\n", idle_ticks);
    printf("      %lld active cpu ticks\n", active_ticks);
    printf("      %lld total cpu ticks\n", total_ticks);
    printf("      %lld pages paged in\n", g_pages_paged_in.load());
    printf("      %lld pages paged out\n", g_pages_paged_out.load());
}

void printProcessSmi() {
    lock_guard<mutex> memory_lock(g_memory_mutex);
    lock_guard<mutex> process_lock(g_process_lists_mutex);

    // Calculate memory statistics
    int total_memory = g_max_overall_mem;
    int used_memory = 0;
    int free_memory = 0;
    
    for (const auto& block : g_memory_blocks) {
        if (block.is_free) {
            free_memory += block.size;
        } else {
            used_memory += block.size;
        }
    }

    cout << "\n";
    cout << "+-----------------------------------------------------------------------------------------+" << endl;
    cout << "| PROCESS-SMI V01.00    Driver Version: 1.1.0                                           |" << endl;
    cout << "+-----------------------------------------------------------------------------------------+" << endl;
    
    // Print CPU utilization info
    long long idle_ticks = g_idle_cpu_ticks.load();
    long long active_ticks = g_active_cpu_ticks.load();
    long long total_ticks = idle_ticks + active_ticks;
    
    double cpu_util = 0.0;
    if (total_ticks > 0) {
        cpu_util = (double)active_ticks / total_ticks * 100.0;
    }

    cout << "| CPU-Util: " << fixed << setprecision(0) << cpu_util << "%      " 
         << "Memory Usage: " << used_memory / 1024 << "MiB / " << total_memory / 1024 << "MiB      "
         << "Memory Util: " << fixed << setprecision(0) 
         << (total_memory > 0 ? (double)used_memory / total_memory * 100.0 : 0.0) << "%   |" << endl;
    
    cout << "+-----------------------------------------------------------------------------------------+" << endl;
    cout << "|                                 Running processes and memory usage:                    |" << endl;
    cout << "+-----------------------------------------------------------------------------------------+" << endl;
    cout << "| Process ID |   Process Name   |    Memory Usage    |" << endl;
    cout << "+-----------------------------------------------------------------------------------------+" << endl;

    // Get memory usage for each process from memory blocks
    map<string, int> process_memory_usage;
    for (const auto& block : g_memory_blocks) {
        if (!block.is_free && !block.process_name.empty()) {
            process_memory_usage[block.process_name] += block.size;
        }
    }

    // Display running processes
    bool has_running_processes = false;
    for (int i = 0; i < config_num_cpu; ++i) {
        if (g_running_processes[i] != nullptr) {
            PCB* process = g_running_processes[i];
            int memory_usage = process_memory_usage[process->name];
            
            cout << "| " << setw(10) << process->id 
                 << " | " << setw(15) << process->name 
                 << " | " << setw(15) << (memory_usage / 1024) << " MiB    |" << endl;
            
            has_running_processes = true;
        }
    }

    if (!has_running_processes) {
        cout << "|            No running processes found                                                   |" << endl;
    }

    cout << "+-----------------------------------------------------------------------------------------+" << endl;
}

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
                cout << "Starting " << config_scheduler << " scheduler with " 
                    << config_num_cpu << " CPU cores..." << endl;
                
                g_keep_generating = true;  // Enable generation
                g_exit_flag = false;  // Make sure exit flag is reset
                g_tick_thread = thread(tick_generator_thread);
                g_scheduler_thread = thread(schedulerThread);
                
                for (int i = 0; i < config_num_cpu; ++i) {
                    if (current_scheduler_type == FCFS) {
                        g_worker_threads.emplace_back(fcfs_worker_thread, i);
                    } else {
                        g_worker_threads.emplace_back(rr_worker_thread, i);
                    }
                }
                g_threads_started = true;
                
                // Start generation thread
                thread([](string screenName) {
                    while (g_keep_generating && !g_exit_flag) {
                        createTestProcesses(screenName);
                        this_thread::sleep_for(chrono::milliseconds(100)); // Faster generation for test
                    }
                }, screen.name).detach();
                
                cout << "Scheduler started. Generating processes for paging test..." << endl;
            } else {
                cout << "Scheduler is already running." << endl;
            }
            screen.currentLine++;
        } else if (screenCmd == "scheduler-stop") {
            cout << "Stopping and resetting the scheduler..." << endl;
            stopAndResetScheduler();
            screen.currentLine++;
        } else if (screenCmd == "process-smi") {
            printProcessSmi();
            screen.currentLine++;
        }  else if (screenCmd == "vmstat") {
            printVmstat();
        }
        else if (screenCmd == "screen -ls") {
            string report = getSystemReport();
            cout << report;
        } else {
            cout << "Unrecognized command. Please try again." << endl;
            screen.currentLine++;
        }
    }
}

void menuSession() {
    bool initialized = false;
    bool firstLoad = true;
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
            istringstream iss(command.substr(10));
            string name;
            int memSize = 0;
            iss >> name >> memSize;

            bool isPowerOf2 = (memSize > 0) && ((memSize & (memSize - 1)) == 0);
            if (name.empty() || memSize < 64 || memSize > 65536 || !isPowerOf2) {
                cout << "Invalid memory allocation. Memory must be a power of 2 between 2^6 (64) and 2^16 (65536) bytes." << endl;
                cout << "Usage: screen -s <process_name> <process_memory_size>" << endl;
            } else if (screens.find(name) != screens.end()) {
                cout << "Screen session already exists: " << name << endl;
                cout << "Created At: " << screens[name].timestamp << endl;
            } else {
                Console newScreen(name, memSize);
                screens[name] = newScreen;
                cout << "New screen session created: " << name << " with memory size: " << memSize << endl;
                screenSession(screens[name]);
                clearScreen();
                printMenuCommands();
            }
        } else if (command.find("screen -r ") == 0) {
            string name = command.substr(10);
            if (name.empty()) {
                cout << "Please provide a name to resume a screen session." << endl;
            } else if (screens.find(name) == screens.end()) {
                cout << "Process " << name << " not found." << endl;
            } else {
                // Check log.txt for memory access violation error
                ifstream logFile("log.txt");
                bool violationFound = false;
                string line, violationTime, violationAddr;
                
                while (getline(logFile, line)) {
                    string searchStr = "process " + name + " violation error";
                    if (line.find(searchStr) != string::npos) {
                        violationFound = true;
                        break;
                    }
                }
                logFile.close();
                
                if (violationFound) {
                    cout << "Process " << name << " shut down due to memory access violation error." << endl;
                } else {
                    screenSession(screens[name]);
                    clearScreen();
                    printMenuCommands();
                }
            } 
        } else if (command == "screen -ls") {
            string report = getSystemReport();
            cout << report;
        } else if (command == "report-util") {
            string report = getSystemReport();

            // Print to console
            cout << report;

            // Export to file (DISABLED)
            ofstream outFile("csopesy-log.txt", ios::app);
            if (outFile.is_open()) {
                outFile << "=== SYSTEM REPORT SAVED AT " << getCurrentTimestampWithMillis() << " ===\n";
                outFile << report << endl;
                outFile.close();
                cout << "Report saved to csopesy-log.txt" << endl;
            } 
            else {
                cout << "Failed to save report to file." << endl;
            }
        }
        else {
            cout << "Unrecognized command. Please try again." << endl;
        }
    }
}