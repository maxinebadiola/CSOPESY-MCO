#include "headers.h"

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
    cout << "4. screen -ls" << endl;
    cout << "5. screen" << endl;
    cout << "6. clear / cls" << endl; 
    cout << "7. exit" << endl;
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
                    while (g_keep_generating) {
                        createTestProcesses(screenName);
                        this_thread::sleep_for(chrono::milliseconds(1000));
                    }
                }, screen.name).detach();
            }
            screen.currentLine++;
        } else if (screenCmd == "scheduler-stop") {
            cout << "Stopping and resetting the scheduler..." << endl;
            stopAndResetScheduler();
            screen.currentLine++;
        } else if (screenCmd == "process-smi") {
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