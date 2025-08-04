#include "headers.h"

void printHeader() {
    cout << R"(
   _____  _____  ____  _____  ______  _______     __
  / ____|/ ____|/ __ \|  __ \|  ____|/ ____\ \   / /
 | |    | (___ | |  | | |__) | |__  | (___  \ \_/ / 
 | |     \___ \| |  | |  ___/|  __|  \___ \  \   /  
 | |____ ____) | |__| | |    | |____ ____) |  | |   
  \_____|_____/ \____/|_|    |______|_____/   |_|   

                  CSOPESY CLI EMULATOR [screen -r test]
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
    cout << "2. screen -c <process_name> <process_memory_size> \"<instructions>\"" << endl;
    cout << "3. screen -r <name>" << endl;
    cout << "4. screen -ls" << endl;
    cout << "5. report-util" << endl;
    cout << "6. clear / cls" << endl;
    cout << "7. exit" << endl;
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
        } else if (command.find("screen -c ") == 0) {
            // Parse screen -c command: screen -c <process_name> <process_memory_size> "<instructions>"
            string remaining = command.substr(10); // Remove "screen -c "
            
            // Find the start and end of the quoted instructions
            size_t quote_start = remaining.find('"');
            size_t quote_end = remaining.rfind('"');
            
            if (quote_start == string::npos || quote_end == string::npos || quote_start == quote_end) {
                cout << "Invalid command format. Usage: screen -c <process_name> <process_memory_size> \"<instructions>\"" << endl;
                continue;
            }
            
            // Extract process name, memory size, and instructions
            string before_quote = remaining.substr(0, quote_start);
            string instructions_str = remaining.substr(quote_start + 1, quote_end - quote_start - 1);
            
            istringstream iss(before_quote);
            string process_name;
            int mem_size = 0;
            
            if (!(iss >> process_name >> mem_size)) {
                cout << "Invalid command format. Usage: screen -c <process_name> <process_memory_size> \"<instructions>\"" << endl;
                continue;
            }
            
            // Validate memory size
            bool isPowerOf2 = (mem_size > 0) && ((mem_size & (mem_size - 1)) == 0);
            if (mem_size < 64 || mem_size > 65536 || !isPowerOf2) {
                cout << "Invalid memory allocation. Memory must be a power of 2 between 2^6 (64) and 2^16 (65536) bytes." << endl;
                continue;
            }
            
            // Parse and validate instructions
            vector<string> instructions = parseInstructions(instructions_str);
            if (!validateInstructions(instructions)) {
                cout << "Invalid command. Instructions must be 1-50 semicolon-separated commands." << endl;
                continue;
            }
            
            // Check if process already exists
            if (screens.find(process_name) != screens.end()) {
                cout << "Screen session already exists: " << process_name << endl;
                cout << "Created At: " << screens[process_name].timestamp << endl;
                continue;
            }
            
            // Initialize memory space if not already done
            if (g_memory_space_size == 0) {
                initializeMemorySpace(g_max_overall_mem); // Use config value
            }
            
            // Create a new process with custom instructions
            static int custom_process_counter = 1;
            auto new_pcb = make_unique<PCB>(
                custom_process_counter++,
                process_name,
                RUNNING,
                time(0),
                static_cast<int>(instructions.size()),
                0,
                "screen_" + process_name + ".txt",
                -1,
                mem_size
            );
            
            new_pcb->custom_instructions = instructions;
            new_pcb->has_custom_instructions = true;
            
            // Create screen session
            Console newScreen(process_name, mem_size);
            screens[process_name] = newScreen;
            
            // Execute the instructions immediately
            try {
                cout << "Executing process " << process_name << " with " << instructions.size() 
                     << " custom instructions..." << endl;
                
                executeInstructionSet(new_pcb->custom_instructions, 0, new_pcb.get());
                new_pcb->instructions_executed = new_pcb->instructions_total;
                new_pcb->state = FINISHED;
                
                cout << "Process " << process_name << " completed successfully." << endl;
                
            } catch (const runtime_error& e) {
                // Handle runtime errors (including memory access violations) 
                cout << "Process " << process_name << " terminated due to: " << e.what() << endl;
                new_pcb->state = FINISHED;
                new_pcb->instructions_executed = new_pcb->instructions_total;
                
                // Check if it's a memory access violation
                string error_msg = e.what();
                if (error_msg.find("Memory access violation") != string::npos) {
                    // Extract memory address from error message (should be in hex format like 0x500)
                    string mem_address = "unknown";
                    size_t pos = error_msg.find("0x");
                    if (pos != string::npos) {
                        size_t end_pos = error_msg.find(" ", pos);
                        if (end_pos != string::npos) {
                            mem_address = error_msg.substr(pos, end_pos - pos);
                        } else {
                            // Take from 0x to the end of string if no space found
                            mem_address = error_msg.substr(pos);
                            // Remove any trailing non-hex characters
                            size_t dash_pos = mem_address.find(" -");
                            if (dash_pos != string::npos) {
                                mem_address = mem_address.substr(0, dash_pos);
                            }
                        }
                    }
                    
                    // Add to cancelled processes list
                    CancelledProcess cancelled;
                    cancelled.process = new_pcb.get();
                    cancelled.timestamp = format_timestamp_for_display(time(0));
                    cancelled.time_only = getTimeOnlyFromTimestamp(getCurrentTimestampWithMillis());
                    cancelled.memory_address = mem_address;
                    
                    // Log to memory violation file
                    ofstream logFile("memory-violation-log.txt", ios::app);
                    if (logFile.is_open()) {
                        logFile << "[" << getCurrentTimestampWithMillis() << "] Process " << process_name 
                                << " terminated due to memory access violation at address " << mem_address << endl;
                        logFile.close();
                    }
                    
                    {
                        lock_guard<mutex> lock(g_cancelled_processes_mutex);
                        g_cancelled_processes.push_back(cancelled);
                    }
                } else {
                    // Not a memory violation, add to finished processes
                    lock_guard<mutex> lock(g_process_lists_mutex);
                    g_finished_processes.push_back(new_pcb.get());
                }
            } catch (const invalid_argument& e) {
                // Handle invalid argument errors
                cout << "Process " << process_name << " terminated due to: " << e.what() << endl;
                new_pcb->state = FINISHED;
                new_pcb->instructions_executed = new_pcb->instructions_total;
                
                // Add to finished processes (non-memory violations)
                lock_guard<mutex> lock(g_process_lists_mutex);
                g_finished_processes.push_back(new_pcb.get());
            } catch (const exception& e) {
                // Handle other exceptions
                cout << "Process " << process_name << " terminated due to: " << e.what() << endl;
                new_pcb->state = FINISHED;
                new_pcb->instructions_executed = new_pcb->instructions_total;
                
                // Add to finished processes (non-memory violations)
                lock_guard<mutex> lock(g_process_lists_mutex);
                g_finished_processes.push_back(new_pcb.get());
            }
            
            // Add to storage - all processes go here regardless of outcome
            g_process_storage.push_back(std::move(new_pcb));
        } else if (command.find("screen -r ") == 0) {
            string name = command.substr(10);
            if (name.empty()) {
                cout << "Please provide a name to resume a screen session." << endl;
            } else {
                // Find the process in the system
                PCB* target_process = nullptr;
                bool process_exists = false;
                
                {
                    lock_guard<mutex> lock(g_process_lists_mutex);
                    
                    // First check running processes
                    for (int i = 0; i < config_num_cpu; ++i) {
                        if (g_running_processes[i] && g_running_processes[i]->name == name) {
                            target_process = g_running_processes[i];
                            process_exists = true;
                            break;
                        }
                    }
                    
                    // If not found in running, check finished processes
                    if (!target_process) {
                        for (const auto& p : g_finished_processes) {
                            if (p->name == name) {
                                target_process = p;
                                process_exists = true;
                                break;
                            }
                        }
                    }
                    
                    // If still not found, check ready queue by searching process storage
                    if (!target_process) {
                        for (const auto& stored_process : g_process_storage) {
                            if (stored_process->name == name) {
                                target_process = stored_process.get();
                                process_exists = true;
                                break;
                            }
                        }
                    }
                }
                
                // Check if process is in cancelled processes list
                bool isCancelled = false;
                CancelledProcess cancelledInfo;
                {
                    lock_guard<mutex> lock(g_cancelled_processes_mutex);
                    for (const auto& cp : g_cancelled_processes) {
                        if (cp.process->name == name) {
                            isCancelled = true;
                            cancelledInfo = cp;
                            process_exists = true;
                            target_process = cp.process;
                            break;
                        }
                    }
                }
                
                if (!process_exists) {
                    cout << "Process " << name << " not found." << endl;
                } else if (isCancelled) {
                    // Process was cancelled due to memory violation
                    cout << "Process " << name << " shut down due to memory access violation error that occurred at "
                         << cancelledInfo.time_only << ". " << cancelledInfo.memory_address << " invalid." << endl;
                } else if (target_process->state == FINISHED) {
                        // Process finished successfully, show its information and output
                        cout << "\n==== PROCESS: " << name << " (FINISHED) ====" << endl;
                        cout << "ID: " << target_process->id << endl;
                        cout << "Created At: " << format_timestamp_for_display(target_process->creation_time) << endl;
                        cout << "Instructions: " << target_process->instructions_executed.load() 
                             << " / " << target_process->instructions_total << endl;
                        cout << "Status: Finished!" << endl;
                        
                        cout << "\n==== PROCESS OUTPUT ====" << endl;
                        if (!target_process->logs.empty()) {
                            for (const auto& log : target_process->logs) {
                                cout << log << endl;
                            }
                        } else {
                            cout << "No output recorded." << endl;
                        }
                        cout << "=========================" << endl;
                    } else {
                        // Process is still running or in ready queue, enter screen session
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