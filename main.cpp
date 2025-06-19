#include <iostream>
#include <string>
#include <cstdlib> 
//W3 NEW: libraries 
#include <map> //screen sessions
#include <ctime> //timestamps
//W6 NEW: FCFS libraries
#include <fstream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <iomanip>

using namespace std;
class Console; //forward declaration for W6 variables [DO NOT REMOVE]

//W6: variables for FCFS scheduler-test
const int totalCores = 4; //cpu cores for sched [0, 1, 2, 3]
const int totalCommands = 100; //print commands per process
const int totalProcesses = 10; //processes to simul
// https://www.geeksforgeeks.org/cpp/std-mutex-in-cpp/
mutex queueMutex; //sync queue
mutex outputMutex; //sync console 
condition_variable schedulerCv; //sched condition
queue<string> processQueue; 
atomic<bool> shutdownFlag(false); //for schedulter-stop (todo)
map<string, bool> isRunning; 
map<string, bool> isFinished; 
map<string, int> completedCount; 
map<string, Console> screens; 


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
   cout << "1. initialize" << endl;
   cout << "2. scheduler-test" << endl; //W6 simulate FCFS processing
   cout << "3. scheduler-stop" << endl;
   cout << "4. report-util" << endl;
   cout << "5. screen" << endl;
   cout << "6. screen -ls" << endl; //W6 print running processes
   cout << "7. clear / cls" << endl; 
   cout << "8. exit" << endl;
}

//W6 functions and threads
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

//COMMENT OUT THE FUNC BELOW FOR W6 SUBMISSION (will slow down program)
void initializeLogs(const string& screenName, int processNum) {
    string filename = "screen_" + screenName + "_process_" + to_string(processNum) + ".txt";
    ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << "Process name: " << screenName << "_" << processNum << endl;
        outFile << "Logs:" << endl;
    }
}
//COMMENT OUT THE FUNC BELOW FOR W6 SUBMISSION (will slow down program)
void exportLogs(const string& screenName, int processNum, int coreId, const string& message) {
    lock_guard<mutex> lock(outputMutex);
    string filename = "screen_" + screenName + "_process_" + to_string(processNum) + ".txt";
    ofstream outFile(filename, ios::app);
    if (outFile.is_open()) {
        outFile << getCurrentTimestampWithMillis() << " Core:" << coreId << " \"" << message << "\"" << endl;
    }
}

void workerThread(int coreId) {
    while (!shutdownFlag) {
        unique_lock<mutex> lock(queueMutex);
        schedulerCv.wait(lock, [] { return !processQueue.empty() || shutdownFlag; });

        if (!processQueue.empty()) {
            string processName = processQueue.front();
            processQueue.pop();
            isRunning[processName] = true;
            lock.unlock();

            // Extract screen name and process number
            size_t underscore_pos = processName.find_last_of('_');
            string screenName = processName.substr(0, underscore_pos);
            int processNum = stoi(processName.substr(underscore_pos + 1));

            // Initialize log file if it doesn't exist
            initializeLogs(screenName, processNum);

            // Process the commands
            for (int i = 0; i < totalCommands; i++) {
                stringstream msg;
                msg << "Printing from " << processName << "! Command " << (i + 1) << "/" << totalCommands;
                exportLogs(screenName, processNum, coreId, msg.str());
                
                // Simulate processing time
                this_thread::sleep_for(chrono::milliseconds(10));
                
                completedCount[processName]++;
            }

            lock.lock();
            isRunning[processName] = false;
            isFinished[processName] = true;
            lock.unlock();
        }
    }
}

void schedulerThread() {
    vector<thread> workers;
    for (int i = 0; i < totalCores; i++) {
        workers.emplace_back(workerThread, i);
    }

    while (!shutdownFlag) {
        this_thread::sleep_for(chrono::seconds(1));
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
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
   
//W3 NEW: screen console class
//  "screen -r <name>" / "screen -s <name>" - Display a placeholder info that contains the following:
//     1. Process name
//     2. Current line of instruction / Total line of instruction.
//     3. Timestamp of when the screen is created in (MM/DD/YYYY, HH:MM:SS AM/PM) format.
class Console {
public:
    string name;
    int currentLine;
    int totalLines;
    string timestamp;

    //default constructor
    Console() : name(""), currentLine(0), totalLines(0), timestamp("") {}

    Console(const string& name, int total) {
        this->name = name;
        currentLine = 0;
        totalLines = total;
        //HH:MM:SS AM/PM
        time_t now = time(0); //https://cplusplus.com/reference/ctime/time/
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

//W6 NEW: process progress console output (screen -ls)
void printSchedProgress() {
    cout << "\n==== RUNNING PROCESSES ====" << endl;
    bool anyRunning = false;
    for (const auto& pair : screens) {
        const string& name = pair.first;
        const Console& console = pair.second;
        if (isRunning[name] && !isFinished[name]) {
            cout << name << " " << getCurrentTimestampWithMillis() << " Core: " 
                 << (completedCount[name] % totalCores) << " " 
                 << completedCount[name] << "/" << totalCommands << endl;
            anyRunning = true;
        }
    }
    if (!anyRunning) {
        cout << "No running processes" << endl;
    }

    cout << "\n==== FINISHED PROCESSES ====" << endl;
    bool anyFinished = false;
    for (const auto& pair : screens) {
        const string& name = pair.first;
        const Console& console = pair.second;
        if (isFinished[name]) {
            cout << name << " " << console.timestamp << " Finished " 
                 << totalCommands << "/" << totalCommands << endl;
            anyFinished = true;
        }
    }
    if (!anyFinished) {
        cout << "No finished processes" << endl;
    }
}

void createTestProcesses(const string& screenName) {
    lock_guard<mutex> lock(queueMutex);
    for (int i = 1; i <= totalProcesses; i++) {
        string processName = screenName + "_" + (i < 10 ? "0" + to_string(i) : to_string(i));
        if (screens.find(processName) == screens.end()) {
            Console newScreen(processName, totalCommands);
            screens[processName] = newScreen;
        }
        processQueue.push(processName);
        isRunning[processName] = false;
        isFinished[processName] = false;
        completedCount[processName] = 0;
    }
    schedulerCv.notify_all();
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
            cout << "\nExiting screen session..." << endl; //TODO: fix menu reprinting
            break; 
        } else if (screenCmd == "clear" || screenCmd == "cls") {
            screen.currentLine++;
            clearScreen();
            cout << "==== SCREEN SESSION: " << screen.name << " ====" << endl;
            screen.displayInfo();
            printScreenCommands();
            continue;
        } else if (screen.currentLine >= screen.totalLines) {
            cout << "100 lines executed. No more commands can be run." << endl;
        } else if (screenCmd == "initialize") {
            cout << "Initialize command recognized." << endl;
            screen.currentLine++;
        } else if (screenCmd == "scheduler-test") 
        {
            createTestProcesses(screen.name); //W6 simulate FCFS processing
            cout << "Started FCFS scheduling for " << totalProcesses << " processes" << endl;
            screen.currentLine++;
        } 
        else if (screenCmd == "scheduler-stop") { //W6 TODO (not in specs)
            cout << "Scheduler-stop command recognized." << endl;
            screen.currentLine++;
        } else if (screenCmd == "report-util") {
            cout << "Report-util command recognized." << endl;
            screen.currentLine++;
        } else if (screenCmd == "screen") {
            screen.currentLine++;
            screen.displayInfo();
        } else if (screenCmd == "screen -ls") { //W6 NEW: list running processes
            printSchedProgress();
            screen.currentLine++;
        }
        else {
            cout << "Unrecognized command. Please try again." << endl;
            screen.currentLine++;
        }
    }
}

//W3 NEW: main menu
void menuSession() {
    printMenuCommands();
    thread scheduler(schedulerThread); //W6 NEW: start scheduler thread
    while (true) {

        cout << "\n> ";
        string command;
        getline(cin, command);

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
            }
        } else if (command.find("screen -r ") == 0) {
            string name = command.substr(10);
            if (name.empty()) {
                cout << "Please provide a name to resume a screen session." << endl;
            } else if (screens.find(name) == screens.end()) {
                cout << "Screen session not found: " << name << endl;
            } else {
                screenSession(screens[name]);
            }
        } else {
            cout << "Unrecognized command. Please try again." << endl;
        }
    }
}

int main() {
    clearScreen();
    menuSession();

    return 0;
}