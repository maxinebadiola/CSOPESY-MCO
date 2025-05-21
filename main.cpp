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

void printCommands() {
   cout << "Available Commands:" << endl;
   cout << "1. initialize" << endl;
   cout << "2. screen -s <name>" << endl;
   cout << "3. screen -r <name>" << endl;
   cout << "4. scheduler-test" << endl;
   cout << "5. scheduler-stop" << endl;
   cout << "6. report-util" << endl;
   cout << "7. clear / cls" << endl;
   cout << "8. exit" << endl;
}

void clearScreen() {
   #if defined(_WIN64)
       system("cls");
   #else
       system("clear");
   #endif
   printHeader();
   printCommands();
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

int main() {
    map<string, Console> screens; // W3 NEW: screen instances
    string command;
    printHeader();
    printCommands();

    while (true) {
        cout << "\n> ";
        getline(cin, command);
        //W2 commands
        if (command == "initialize") {
            cout << "Initialize command recognized. Doing something." << endl;
        } else if (command == "scheduler-test") {
            cout << "Scheduler-test command recognized. Doing something." << endl;
        } else if (command == "scheduler-stop") {
            cout << "Scheduler-stop command recognized. Doing something." << endl;
        } else if (command == "report-util") {
            cout << "Report-util command recognized. Doing something." << endl;
        } else if (command == "clear" || command == "cls") {
            clearScreen();
        } 
        //W3 NEW: create new screen (screen -s <name>)
        else if (command.find("screen -s ") == 0) {
            string name = command.substr(10); // get <name> (all characters after "screen -s ")
            if (name.empty()) { //empty <name>
                cout << "Input name for the screen session." << endl;
            } else if (screens.find(name) != screens.end()) { //duplicate
                cout << "Screen session already exists: " << name << endl;
                cout << "Created At: " << screens[name].timestamp << endl;
            } else { //VALID
                Console newScreen(name, 100);
                screens[name] = newScreen;
                cout << "New screen session created: " << name << endl;
                screens[name].displayInfo();
            }
        } 
        //W3 NEW: resume screen session (screen -r <name>)
        else if (command.find("screen -r ") == 0) {
            string name = command.substr(10); // get <name>
            if (name.empty()) { //empty <name>
                cout << "Input name of the screen session" << endl;
            } else if (screens.find(name) == screens.end()) { //not found
                cout << "Screen session not found: " << name << endl;
            } else { //VALID
                while (true) {
                    clearScreen();
                    cout << "Resuming screen session: " << name << endl;
                    screens[name].displayInfo();
                    //TODO: actual functionality (e.g., incrementing current line, exiting the screen session etc.)
                    //note: i think weneed to make yung actual int main commands a function so pwede icall dto
                }
                clearScreen();
            }
        }
        else if (command == "exit") { //TODO: exit command from screen, and exit from program
            cout << "Exit command recognized. Exiting Program..." << endl;
            break;
        } else {
            cout << "Unrecognized command. Please try again." << endl;
        }
    } //while loop end

    return 0;
}
