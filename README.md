This program creates a library called tdmm which is a custom memory manager.

It contains two functions, t_malloc and t_free. These functions are implemented using a LinkedList structure where each memory block contains a 32 byte header.

To build the library, run ./build.sh. main.c contains functions that run trials that can be used by plot.py to build statistics graphs. 
