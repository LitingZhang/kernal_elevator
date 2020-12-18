
## Part1

Add 7 more system calls and use `strace` to trace syscalls and signals

## Part2
This part create a kernel module called `my_timer` that calls `current_kernel_time()` and stores the values. `current_kernel_time()` holds the number of seconds and nanoseconds since the Epoch.</br>
When `my_timer` is loaded (using insmod), it create a proc entry called `/proc/timer`. </br>
When `my_timer` is unloaded (using rmmod), `/proc/timer` should be removed. </br>
On each read you will use the proc interface to both print the current time as well as the amount of time that's
passed since the last call (if valid).
#### Usage 
* run `make` to compile
* insert the module using `sudo insmod my_timer.ko`
* print out the proc file using `cat /proc/my_timer`
* remove module using `sudo rmmod my_timer`

## Part3 Elevator
This part implement a scheduling algorithm for an elevator for human and zombia. </br>

This part is implemented under kernel 4.19.98. Downloaded from https://www.kernel.org/pub/linux/kernel/v4.x/linux-4.19.98.tar.xz </br>

### Add system calls
Add the following files under syscall to a directory
* /usr/src/test_kernel/SystemCalls/part3.c
* /usr/src/test_kernel/SystemCalls/Makefile

Modify or replace the following files:
* /usr/src/test_kernel/arch/x86/entry/syscalls/syscall_64.tbl
* /usr/src/test_kernel/include/linux/syscalls.h
* /usr/src/test_kernel/Makefile
    * Search for the second occurrence of core-y. Add the SystemCalls directory to the list. These are the directories that have files to be built directly into the kernel.
    * `core-y += kernel/ certs/ nm/ fs/ ipc/ security/ crypto/ block/ Systemcalls/`
 
Compile:
* `sudo apt-get install build-essential libncurses-dev bison flex libssl-dev libelf-dev`
* `make menuconfig`, use esc to exit and save. 
* `sudo make -j $(nproc)`
* `sudo make modules_install install`
* Reboot

### Usage 
* Run `make` to compile or using userspace program
* Use`insert` to insert the elevator module
* Use  `remove` to remove the elevator module
* Use `start` to start the elevator
* Use `stop` to stop the elvator
* Use `watch_proc` to watch teh proc file
* Use `clean` to clean the files

### Known Bugs
* Part 3: picks people up after stop
* Part 3: does not stop or finish dropping off passengers
* Part 3: could not issue requsts after stop
* Part 3: missing some locks
