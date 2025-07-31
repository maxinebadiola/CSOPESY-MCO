#include "headers.h"

mutex outputMutex;
map<string, Console> screens;
unordered_map<string, uint16_t> variables;
random_device rd;
mt19937 gen(rd());
uniform_int_distribution<uint16_t> dist(0, 65535);
bool enable_sleep = false;
bool enable_for = false;

void DECLARE(const string& var, uint16_t value) {
    variables[var] = value;
}

uint16_t getValue(const string& varOrValue) {
    if (variables.count(varOrValue)) return variables[varOrValue];
    try {
        return static_cast<uint16_t>(stoi(varOrValue));
    } catch (const exception&) {
        return 0; 
    }
}

void ADD(const string& var1, const string& op2, const string& op3) {
    if (!variables.count(var1)) variables[var1] = 0;
    uint16_t val2 = getValue(op2);
    uint16_t val3 = getValue(op3);
    uint32_t sum = static_cast<uint32_t>(val2) + static_cast<uint32_t>(val3);
    if (sum > 65535) sum = 65535;
    variables[var1] = static_cast<uint16_t>(sum);
}

void SUBTRACT(const string& var1, const string& op2, const string& op3) {
    if (!variables.count(var1)) variables[var1] = 0;
    uint16_t val2 = getValue(op2);
    uint16_t val3 = getValue(op3);
    int32_t diff = static_cast<int32_t>(val2) - static_cast<int32_t>(val3);
    if (diff < 0) diff = 0;
    variables[var1] = static_cast<uint16_t>(diff);
}

double setVariableDefault() {
    for (auto& kv : variables) kv.second = 0;
    return 0;
}

string randomVariable() {
    static const vector<string> vars = {"var1", "var2", "var3"};
    uniform_int_distribution<int> pick(0, 2);
    return vars[pick(gen)];
}

string randomUint16Value() {
    return to_string(dist(gen));
}

string randomVarOrValue() {
    if (dist(gen) % 2) return randomVariable();
    return randomUint16Value();
}

void PRINT(const string& msg, PCB* current_process, const string& process_name, const string& screen_name) {
    string output = msg;
    
    if (msg.empty() && !process_name.empty()) {
        output = "Hello world from " + process_name + "!";
    }
    
    for (const auto& var : variables) {
        string varName = var.first;
        string varValue = to_string(var.second);
        
        size_t pos = 0;
        while ((pos = output.find(varName, pos)) != string::npos) {
            bool isWholeVar = true;
            if (pos > 0 && (isalnum(output[pos - 1]) || output[pos - 1] == '_')) {
                isWholeVar = false;
            }
            if (pos + varName.length() < output.length() && 
                (isalnum(output[pos + varName.length()]) || output[pos + varName.length()] == '_')) {
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
        lock_guard<mutex> lock(outputMutex);
        current_process->logs.push_back(output);
    } else {
        lock_guard<mutex> lock(outputMutex);
        cout << output << endl;
    }
}

void SLEEP(uint8_t ticks) {
    if (ticks == 0) return;
    this_thread::sleep_for(chrono::milliseconds(config_delay_per_exec * ticks));
}

void FOR(const vector<string>& instructions, int repeats, int nestingLevel, PCB* current_process) {
    if (nestingLevel >= 3) {
        lock_guard<mutex> lock(outputMutex);
        cout << "Maximum nesting level (3) reached. Skipping nested FOR loop." << endl;
        return;
    }

    if (repeats < 0) repeats = 0;
    if (repeats > 100) repeats = 100; 
    
    for (int i = 0; i < repeats; ++i) {
        executeInstructionSet(instructions, nestingLevel + 1, current_process);
        if (g_exit_flag) break;
    }
}

void executeInstructionSet(const vector<string>& instructions, int nestingLevel, PCB* current_process) {
    for (const string& instruction : instructions) {
        if (g_exit_flag) break; 
        
        istringstream iss(instruction);
        string command;
        iss >> command;
        
        if (command == "DECLARE") {
            string var;
            string valueStr;
            if (iss >> var >> valueStr) {
                try {
                    uint16_t value = static_cast<uint16_t>(stoi(valueStr));
                    DECLARE(var, value);
                } catch (const exception&) {
                    DECLARE(var, 0);
                }
            }
        }
        else if (command == "ADD") {
            string var1, op2, op3;
            if (iss >> var1 >> op2 >> op3) {
                ADD(var1, op2, op3);
            }
        }
        else if (command == "SUBTRACT") {
            string var1, op2, op3;
            if (iss >> var1 >> op2 >> op3) {
                SUBTRACT(var1, op2, op3);
            }
        }
        else if (command == "PRINT") {
            string msg;
            getline(iss, msg);

            if (!msg.empty()) {
                msg.erase(0, msg.find_first_not_of(" \t"));
                if (msg.length() >= 2 && msg.front() == '"' && msg.back() == '"') {
                    msg = msg.substr(1, msg.length() - 2);
                }
            }
            PRINT(msg, current_process);
        }
        else if (command == "SLEEP") {
            string ticksStr;
            if (iss >> ticksStr) {
                try {
                    uint8_t ticks = static_cast<uint8_t>(stoi(ticksStr));
                    SLEEP(ticks);
                } catch (const exception&) {
                    // Skip invalid SLEEP commands
                }
            }
        }
        else if (command == "FOR") {
            string line;
            getline(iss, line);
            
            size_t lastSpace = line.find_last_of(' ');
            if (lastSpace != string::npos) {
                string instructionsStr = line.substr(0, lastSpace);
                string repeatsStr = line.substr(lastSpace + 1);
                
                instructionsStr.erase(0, instructionsStr.find_first_not_of(" \t"));
                instructionsStr.erase(instructionsStr.find_last_not_of(" \t") + 1);
                
                int repeats = 0;
                try {
                    repeats = stoi(repeatsStr);
                } catch (const exception&) {
                    repeats = 0;
                }
                
                vector<string> forInstructions;
                istringstream instrStream(instructionsStr);
                string singleInstr;
                
                while (getline(instrStream, singleInstr, ',')) {
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

vector<string> generateRandomInstructions(const string& processName, int count, bool enable_sleep, bool enable_for) {
    vector<string> instructions;

    vector<int> possibleInstructions = {0, 1, 2, 3, 4};
    if (enable_sleep) possibleInstructions.push_back(5);
    if (enable_for) possibleInstructions.push_back(6);

    uniform_int_distribution<int> instrType(0, possibleInstructions.size() - 1);

    for (int i = 0; i < count; ++i) {
        switch (possibleInstructions[instrType(gen)]) {
            case 0:
                instructions.push_back("DECLARE " + randomVariable() + " " + randomUint16Value());
                break;
            case 1:
                instructions.push_back("ADD " + randomVariable() + " " + randomVarOrValue() + " " + randomVarOrValue());
                break;
            case 2:
                instructions.push_back("SUBTRACT " + randomVariable() + " " + randomVarOrValue() + " " + randomVarOrValue());
                break;
            case 3:
                {
                    string var = randomVariable();
                    instructions.push_back("PRINT \"Value of " + var + " is " + var + "\"");
                }
                break;
            case 4:
                instructions.push_back("PRINT \"Hello world from " + processName + "!\"");
                break;
            case 5:
                instructions.push_back("SLEEP " + to_string(rand() % 1000));
                break;
            case 6:
                instructions.push_back("FOR " + to_string(rand() % 5 + 1));
                break;
        }
    }

    return instructions;
}

void printVarValues() {
    cout << "Values of\n";
    cout << "var1 = " << variables["var1"] << endl;
    cout << "var2 = " << variables["var2"] << endl;
    cout << "var3 = " << variables["var3"] << endl;
}