#include <iostream>
#include <string>
#include <cstdlib> 
//W3 NEW: libraries 
#include <map> //screen sessions
#include <ctime> //timestamps

using namespace std;

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
   cout << "2. scheduler-test" << endl;
   cout << "3. scheduler-stop" << endl;
   cout << "4. report-util" << endl;
   cout << "5. screen" << endl;
   cout << "6. clear / cls" << endl; 
   cout << "7. exit" << endl;
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
            cout << "Exiting screen session..." << endl; //TODO: fix menu reprinting
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
        } else if (screenCmd == "scheduler-test") {
            cout << "Scheduler-test command recognized." << endl;
            screen.currentLine++;
        } else if (screenCmd == "scheduler-stop") {
            cout << "Scheduler-stop command recognized." << endl;
            screen.currentLine++;
        } else if (screenCmd == "report-util") {
            cout << "Report-util command recognized." << endl;
            screen.currentLine++;
        } else if (screenCmd == "screen") {
            screen.currentLine++;
            screen.displayInfo();
        } else {
            cout << "Unrecognized command. Please try again." << endl;
            screen.currentLine++;
        }
    }
}

//W3 NEW: main menu
void menuSession(map<string, Console>& screens) {
     printMenuCommands();
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
    map<string, Console> screens;
    clearScreen();
    menuSession(screens);

    return 0;
}