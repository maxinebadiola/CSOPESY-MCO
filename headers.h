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
    unordered_map<string, uint16_t> symbol_table; // process-specific variables (max 32)
    vector<string> custom_instructions; //screen -c
    bool has_custom_instructions{false};

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
string getTimeOnlyFromTimestamp(const string& timestamp);
string format_timestamp_for_display(time_t t);
void tick_generator_thread();
void stopAndResetScheduler();
void schedulerThread();
void clearScreen();
string getSystemReport();
string getVMStatReport();
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

// Cancelled process tracking (memory violations)
struct CancelledProcess {
    PCB* process;
    string timestamp;
    string time_only;  //(HH:MM:SS AM/PM)
    string memory_address;
};
extern vector<CancelledProcess> g_cancelled_processes;
extern mutex g_cancelled_processes_mutex;

extern mutex g_process_lists_mutex;
extern atomic<bool> g_exit_flag;
extern vector<unique_ptr<PCB>> g_process_storage;
extern thread g_scheduler_thread;
extern vector<thread> g_worker_threads;
extern atomic<bool> g_threads_started;
extern const int TICK_DURATION_MS;
extern atomic<unsigned long long> g_cpu_ticks;
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

// Page structure for demand paging
struct Page {
    int virtual_page_number;
    int physical_frame_number;
    bool is_in_memory;
    bool is_dirty;
    string process_name;
    int last_access_time;
    
    Page() : virtual_page_number(-1), physical_frame_number(-1), 
             is_in_memory(false), is_dirty(false), process_name(""), 
             last_access_time(0) {}
};

// Frame structure for physical memory
struct Frame {
    bool is_free;
    int virtual_page_number;
    string process_name;
    int last_access_time;
    
    Frame() : is_free(true), virtual_page_number(-1), 
              process_name(""), last_access_time(0) {}
};

// Memory space for processes
extern vector<uint16_t> g_memory_space;
extern mutex g_memory_space_mutex;
extern int g_memory_space_size;

extern vector<MemoryBlock> g_memory_blocks;
extern mutex g_memory_mutex;
extern int g_max_overall_mem;
extern int g_mem_per_frame;
extern int g_min_mem_per_proc;
extern int g_max_mem_per_proc;

// Demand paging structures
extern vector<Frame> g_physical_frames;
extern map<string, vector<Page>> g_process_page_tables;
extern mutex g_paging_mutex;
extern int g_total_frames;
extern atomic<int> g_access_counter;

// Statistics tracking
extern atomic<unsigned long long> g_total_cpu_ticks;
extern atomic<unsigned long long> g_idle_cpu_ticks;
extern atomic<unsigned long long> g_active_cpu_ticks;
extern atomic<int> g_pages_paged_in;
extern atomic<int> g_pages_paged_out;

void initializeMemory();
void initializeMemorySpace(int size);
bool allocateMemoryFirstFit(PCB* process);
void deallocateMemory(PCB* process);
void printMemorySnapshot(const string& filename);
int calculatePagesRequired(int memorySize);
bool isValidMemoryAddress(int address);
uint16_t readMemory(int address);
void writeMemory(int address, uint16_t value);

// Demand paging functions
void initializePaging();
bool allocateMemoryPaging(PCB* process);
void deallocateMemoryPaging(PCB* process);
int handlePageFault(const string& process_name, int virtual_page);
int findFreeFrame();
int selectVictimFrame();
void loadPageFromBackingStore(const string& process_name, int virtual_page, int frame_number);
void savePageToBackingStore(const string& process_name, int virtual_page, int frame_number);
bool isPageInMemory(const string& process_name, int virtual_page);
int getPhysicalAddress(const string& process_name, int virtual_address);
void printPagingState(const string& context);

void printMemoryState(const char* context);

// Instruction execution
void DECLARE(const string& var, uint16_t value, PCB* current_process = nullptr);
uint16_t getValue(const string& varOrValue, PCB* current_process = nullptr);
void ADD(const string& var1, const string& op2, const string& op3, PCB* current_process = nullptr);
void SUBTRACT(const string& var1, const string& op2, const string& op3, PCB* current_process = nullptr);
void READ(const string& var, const string& address_str, PCB* current_process);
void WRITE(const string& address_str, const string& value_str, PCB* current_process);
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
vector<string> parseInstructions(const string& instructions_str);
bool validateInstructions(const vector<string>& instructions);
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