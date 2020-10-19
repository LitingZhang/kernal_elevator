# COP_Proj2

### Part1

Add 7 more system calls. 

### Part2
* run `make` to compile
* insert the module using `sudo insmod my_timer.ko`
* print out the proc file using `cat /proc/my_timer`
* remove module using `sudo rmmod my_timer`

### Part3
* run `make` to compile or using userspace program
* insert the module using `sudo insmod my_timer.ko` 
* print out the proc file using `cat /proc/my_timer`
* remove module using `sudo rmmod elevator`

### Known Bugs
* When issuing second request, a deadlock will occur
* Proc not working
