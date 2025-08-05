## Requirements

* C++ compiler (e.g., `g++`) with C++17 support
* Windows environment (for `.exe` file)

## Compilation

Open your terminal or command prompt in the project directory and run:

```bash
g++ -std=c++17 -pthread -o main.exe main.cpp menu.cpp process.cpp memory.cpp config.cpp instructions.cpp utils.cpp
```

This compiles the program and creates an executable named `main.exe`.

## Running

After successful compilation, run the program with:

```bash
.\main.exe
```
---

## Optional Features

1. **Enable** `SLEEP` / `FOR`

   * Allows the `SLEEP` and `FOR` commands to be included in the randomly generated instruction set when using `scheduler-start`.

2. **Enable** `DEBUG`

   * Turns on detailed debugging messages of instruction processing for `scheduler-start`.
---
