# COMP304 Assignment 1 - Shell-ish

Part 1
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









Part 2 Features (I/O Redirection & Piping)
I/O Redirection

Shell-ish supports basic I/O redirection:

>outputfile : redirect stdout to a file (create if not exists, truncate if exists)

>>outputfile : redirect stdout to a file (append if exists, create if not)

<inputfile : redirect stdin from a file

Note: The provided parser supports redirection targets without spaces (e.g., >out.txt, not > out.txt).

Example:
echo hello >out.txt
echo world >>out.txt
wc <out.txt

Piping

Shell-ish supports piping using the | operator, including chained pipes.

Examples:
ls | wc
ls -la | grep shellish
ls -la | grep shellish | wc

Screenshots

Screenshots for Part 2 are available in the imgs/ folder.






## Part 3 Features (New Built-in Commands)

### cut (built-in)
A simplified version of UNIX `cut` is implemented as a built-in command.
It reads from standard input and prints only the selected fields.

- Default delimiter: TAB
- `-d X` / `--delimiter X` : use character `X` as delimiter
- `-f list` / `--fields list` : comma-separated 1-based field indices (e.g., `1,3,10`)

Example:
cat /etc/passwd | cut -d ":" -f 1,6

### chatroom (built-in)
A simple group chat command using named pipes (FIFOs).

Usage:
chatroom <roomname> <username>

- Room directory: /tmp/chatroom-<roomname>/
- Each user has a FIFO: /tmp/chatroom-<roomname>/<username>
- Type `/exit` to leave the chatroom

### Custom command: repeat (built-in)
A custom built-in command that runs another command multiple times.

Usage:
repeat N <command> [args...]

Examples:
repeat 3 date
repeat 2 ls

### Screenshots
Screenshots for Part 3 are included in the `imgs/` folder.
