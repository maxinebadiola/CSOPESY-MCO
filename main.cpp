#include <iostream>
#include <string>
#include <cstdlib> 

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
   cout << "2. screen" << endl;
   cout << "3. scheduler-test" << endl;
   cout << "4. scheduler-stop" << endl;
   cout << "5. report-util" << endl;
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
   
   int main() {
       string command;
       printHeader();
       printCommands();
   
       while (true) {
           cout << "\n> ";
           getline(cin, command);
   
           if (command == "initialize") {
               cout << "Initialize command recognized. Doing something." << endl;
           } else if (command == "screen") {
               cout << "Screen command recognized. Doing something." << endl;
           } else if (command == "scheduler-test") {
               cout << "Scheduler-test command recognized. Doing something." << endl;
           } else if (command == "scheduler-stop") {
               cout << "Scheduler-stop command recognized. Doing something." << endl;
           } else if (command == "report-util") {
               cout << "Report-util command recognized. Doing something." << endl;
           } else if (command == "clear" || command == "cls") {
               clearScreen();
           } else if (command == "exit") {
               cout << "Exit command recognized. Exiting..." << endl;
               break;
           } else {
               cout << "Unrecognized command. Please try again." << endl;
           }
       }
   
       return 0;
   }