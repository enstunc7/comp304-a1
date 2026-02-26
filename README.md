# COMP304 Assignment 1 - Shell-ish
Shell-ish
This repository contains my implementation of the Shell-ish assignment for COMP 304 Operating Systems.

Repository
GitHub Repo: https://github.com/enstunc7/comp304-a1

Build
To compile the program, run:
gcc -o shell-ish shellish-skeleton.c

Run
To run the shell:
./shell-ish
For better line editing, you can also run:
rlwrap ./shell-ish

Implemented Features in Part 1
The following features are implemented in Part 1:
•	Interactive shell prompt
•	Parsing commands and arguments
•	Builtin exit command
•	Builtin cd command
•	Execution of external commands by creating a child process
•	Manual executable path resolution using execv()
•	Background execution with &
•	Error handling for unknown commands

Example Commands
Some example commands supported in Part 1:
•	ls
•	pwd
•	date
•	cd ..
•	sleep 5
•	sleep 5 &
•	abc

Notes
•	The implementation is based on the provided skeleton file.
•	External commands are executed in child processes.
•	The shell manually searches the PATH variable and runs commands using execv() instead of execvp().

Screenshots
Screenshots of the implemented features are included in the imgs/ folder.
