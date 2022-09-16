# Multithreaded Assignment

This is a project I was given as the final exam for the Laboratory 2 course.

farm is a program in which there are 2 processes: MasterWorker (main) and Collector (child process).

- MasterWorker is a multithreaded process that has 'n' threads, default is 4 and can be changed from the user by using '-n' option.

Each of these threads (named Workers from now on) get one task from the MasterWorker, the task is a regular binary file (checked if regular by MasterWorker), and they do some operations with the content of the files.

Then, Workers send their results and name of the processed file to the Collector, via a socket connection established between the latter and the MasterWorker.

MasterWorker also handles the signals. When a signal (SIGHUP, SIGINT, SIGQUIT, SIGTERM) is recieved, the Workers stop accepting new requests from the MasterWorker, but files that were planned to be processed already are still being processed.

Eventually, Workers are shut down, Collector process is killed and the program is forced to end.

- Collector just waits to recieve data from Workers and prints it on the console.

# How to use
There is a makefile that compiles farm.c and generafile.c and then a test.sh is runned.

This test.sh uses 'generafile' to generate the binary files and then proceeds to run some tests for 'farm'.

Using:
```
make
```
should do the trick.

You can even test the farm program by yourself, with this command:
```
gcc -pthread -o farm farm.c
./farm '-n' <integer> '-q' <integer> '-t' <integer> <list of files>
```
Options:
- '-n' <integer> : number of threads (default is 4).
- '-q' <integer> : number of requests that can be made by MasterWorker at the same time (default is 8).
- '-t' <integer> : the delay between each request made by MasterWorker (in milliseconds, default is 0).

The order of the options is irrilevant and the options can be omitted in the command.
