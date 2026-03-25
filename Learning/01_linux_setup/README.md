# Module 01 — Linux Environment Setup and Development Tools

## Learning Objectives

By the end of this module you will be able to:
- Install and configure an Ubuntu 22.04 development environment
- Navigate the Linux filesystem and use essential shell commands
- Manage packages with `apt`
- Compile and run a C++ programme using `g++` and `make`
- Use `git` for version control

---

## 1. Why Linux?

CauseDeb is built on Linux because:
- **POSIX APIs** give precise control over sockets, clocks, signals, and processes
- **`clock_gettime`** with `CLOCK_MONOTONIC` provides the high-resolution timing we need
- **`pthreads`** (via `std::thread`) give predictable thread behaviour
- The toolchain (`g++`, `gdb`, `valgrind`, `perf`) is excellent and free

---

## 2. Installing Ubuntu 22.04

Use one of:
- **WSL 2** on Windows: `wsl --install -d Ubuntu-22.04`
- **VirtualBox / VMware** with an Ubuntu 22.04 ISO
- **A native installation** on a spare machine or partition
- **A cloud VM** (AWS EC2 t3.micro free tier runs Ubuntu 22.04)

---

## 3. Essential Shell Commands

```bash
# Filesystem navigation
pwd              # print working directory
ls -la           # list files with permissions
cd ~/projects    # change directory
mkdir causedeb   # create directory
rm -rf build/    # remove directory recursively (careful!)

# File viewing
cat file.txt     # print whole file
less file.txt    # page through a file (q to quit)
head -20 file    # first 20 lines
tail -f log.txt  # follow a growing file (Ctrl-C to stop)

# Searching
grep -r "vector_clock" src/   # recursive text search
find . -name "*.cpp"          # find files by name

# Process management
ps aux | grep causedeb   # list running processes
kill -9 <PID>            # force-terminate a process
top                      # live process monitor (q to quit)
```

---

## 4. Package Management with `apt`

```bash
sudo apt update                # refresh package lists
sudo apt upgrade               # upgrade installed packages
sudo apt install build-essential cmake git valgrind gdb
```

`build-essential` installs `gcc`, `g++`, `make`, and related tools.

---

## 5. Your First C++ Programme

Create `hello.cpp`:
```cpp
#include <iostream>
int main() {
    std::cout << "Hello from CauseDeb!\n";
    return 0;
}
```

Compile and run:
```bash
g++ -std=c++17 -Wall -o hello hello.cpp
./hello
```

---

## 6. Building with CMake

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The `-j$(nproc)` flag parallelises the build across all CPU cores.

---

## 7. Version Control with `git`

```bash
git init
git add .
git commit -m "Initial commit"
git log --oneline   # compact history
git diff            # unstaged changes
```

---

## Mini Project

1. Install Ubuntu 22.04 (or WSL 2) and install the required packages.
2. Clone the CauseDeb repository and build it with CMake.
3. Verify that `causedeb_collector`, `causedeb_replay`, and `causedeb_analyze`
   all appear in the `build/` directory.

## Exercises

1. What does the `-j` flag do in `make -j4`?
2. What is the difference between `CLOCK_REALTIME` and `CLOCK_MONOTONIC`?
3. Find all `.h` files under `src/` using `find`.
