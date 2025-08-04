#ifndef HEADERS_H
#define HEADERS_H

#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
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

// Enums and structs
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
    atomic<bool> is_allocated{false};
    int memory_requirement;

    PCB(int p_id, const string& p_name, ProcessState p_state, time_t p_creation_time, 
        int p_instr_total, int p_instr_exec, const string& p_filename, int p_core_id, int p_mem_req)
      : id(p_id), name(p_name), state(p_state), creation_time(p_creation_time),
        instructions_total(p_instr_total), instructions_executed(p_instr_exec), 
        output_filename(p_filename), core_id(p_core_id), remaining_quantum(0), logs(), memory_requirement(p_mem_req) {}
};

// Forward declarations
class Console;
void printConfigVars();
void readConfigFile();
void printHeader();
void printInitial();
void printMenuCommands();
void printScreenCommands();
string getCurrentTimestampWithMillis();
string format_timestamp_for_display(time_t t);
void tick_generator_thread();
void stopAndResetScheduler();
void schedulerThread();
void clearScreen();
string getSystemReport();
void createTestProcesses(const string& screenName);
void menuSession();
void screenSession(Console& screen);
void fcfs_worker_thread(int core_id);
void rr_worker_thread(int core_id);

// Global variables
extern queue<PCB*> g_ready_queue;
extern mutex g_ready_queue_mutex;
extern vector<PCB*> g_running_processes;
extern vector<PCB*> g_finished_processes;
extern mutex g_process_lists_mutex;
extern atomic<bool> g_exit_flag;
extern vector<unique_ptr<PCB>> g_process_storage;
extern thread g_scheduler_thread;
extern vector<thread> g_worker_threads;
extern atomic<bool> g_threads_started;
extern const int TICK_DURATION_MS;
extern atomic<unsigned long long> g_cpu_ticks;
extern atomic<long long> g_idle_cpu_ticks;
extern atomic<long long> g_active_cpu_ticks;
extern atomic<long long> g_pages_paged_in;
extern atomic<long long> g_pages_paged_out;
extern mutex g_tick_mutex;
extern condition_variable g_tick_cv;
extern thread g_tick_thread;
extern mutex outputMutex;
extern map<string, Console> screens;
extern unordered_map<string, uint16_t> variables;
extern random_device rd;
extern mt19937 gen;
extern uniform_int_distribution<uint16_t> dist;
extern bool enable_sleep;
extern bool enable_for;
extern atomic<bool> g_keep_generating;

// Config variables
extern int config_num_cpu;
extern string config_scheduler;
extern int config_quantum_cycles;
extern int config_batch_process_freq;
extern int config_min_ins;
extern int config_max_ins;
extern int config_delay_per_exec;
extern SchedulerType current_scheduler_type;

// memory management
struct MemoryBlock {
    int start_address;
    int size;
    bool is_free;
    string process_name;
};

extern vector<MemoryBlock> g_memory_blocks;
extern mutex g_memory_mutex;
extern int g_max_overall_mem;
extern int g_mem_per_frame;
extern int g_min_mem_per_proc;
extern int g_max_mem_per_proc;

void initializeMemory();
bool allocateMemoryFirstFit(PCB* process);
void deallocateMemory(PCB* process);
void printMemorySnapshot(const string& filename);
int calculatePagesRequired(int memorySize);

void printMemoryState(const char* context);

// Instruction execution
void DECLARE(const string& var, uint16_t value);
uint16_t getValue(const string& varOrValue);
void ADD(const string& var1, const string& op2, const string& op3);
void SUBTRACT(const string& var1, const string& op2, const string& op3);
double setVariableDefault();
string randomVariable();
string randomUint16Value();
string randomVarOrValue();
void PRINT(const string& msg, PCB* current_process = nullptr, 
          const string& process_name = "", const string& screen_name = "");
void SLEEP(uint8_t ticks);
void FOR(const vector<string>& instructions, int repeats, int nestingLevel, PCB* current_process);
void executeInstructionSet(const vector<string>& instructions, int nestingLevel, PCB* current_process);
vector<string> generateRandomInstructions(const string& processName, int count, 
                                        bool enable_sleep, bool enable_for);
void printVarValues();

// Console class
class Console {
public:
    string name;
    int currentLine;
    int totalLines;
    string timestamp;
    Console() : name(""), currentLine(0), totalLines(0), timestamp("") {}
    Console(const string& name, int total);
    void displayInfo();
};

#endif