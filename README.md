# BShell - simple bash-like shell

This is an implementation of a simple bash-like shell.

Supported features:

-   `exec` system call
-   multiple `|` piping
-   `<` input redirection
-   `>` output redirection
-   builtin commands including `cd`, `help`, `exit`, `jobs`, `fg`, `bg`

# Usage

## How to build BShell

`make` command executes a simple command to build the executable named `bshell`.

After building BShell, you can run BShell by typing `./bshell` in the same directory.

## Execution Examples

When you start BShell by typing `./bshell`, it starts to prompt input by printing `$ ` on your terminal.
When you finish typing command and hit Enter, BShell executes the given command, after which it prompts your input again by showing `$ `. This is implemented as an infinite loop.
You can exit BShell by executing `exit` builtin command or typing `Ctrl+D`

Note that BShell process handles `SIGINT` or `SIGSTP` by the default behavior `SIG_DFL`. Thus, BShell process can be killed or stopped by typing `Ctrl+C` or `Ctrl+Z`, unlike many other shell implementations, which ignores some of them.

### Simple Command

```
$ cat bshell.c
  => show the content of 'bshell.c'
```

### Piping

```
$ cat bshell.c | grep static | wc -l
  => show the number of lines including the string "static" in 'bshell.c'
```

### Input Redirection

```
$ cat < bshell.txt | wc -l
  => show the number of lines in 'bshell.c'
```

The following is the default behavior of bash, as long as I observed correctly.
I have mimicked and implemented this behavior in BShell.

```
$ cat < bshell.h < bshell.c < main.c | wc -l
  => show the number of lines in 'main.c'
```

### Output Redirection

```
$ cat bshell.c | grep static | wc -l > a.txt
  => write into 'a.txt' the number of lines including the string "static" in 'bshell.c'
```

```
$ wc -l < bshell.c > a.txt
  => write into 'a.txt' the number of lines in 'bshell.c'
```

The following is the default behavior of bash, as long as I observed correctly.
I have mimicked and implemented this behavior in BShell.

```
$ cat bshell.c | grep static | wc -l > a.txt > b.txt > c.txt
  => write into 'c.txt' the number of lines including the string "static" in 'bshell.c'
     'a.txt' and 'b.txt' get created and the contents are set empty.
```

### Job Controls

Each job has a state, which is `Running`, `Stopped`, or `Finished`.

-   When you add a new job to background by adding `&` after commands, the new job is marked `Running`.
-   When you suspend a foreground job by typing `Ctrl+Z`, the new job is marked `Stopped`.
-   When BShell notices a job is finished, it marks the job `Finished`

Each job has a number (for clarity, I call it index here) which is specified in `fg` and `bg` builtin command calls.
When you add a new job to background, the index of the new job is determined in the following way, which is the default behavior of bash.

-   If there exists no job, the new index is 1
-   If there exist at least one `Finished` jobs, the new index is the minimum of the indice of these `Finished` jobs.
-   If all jobs are marked `Running` or `Stopped`, the new index is the next number of the index of last job

> In bash, when it reports the status of the background jobs (for example, when you run `jobs` builtin command), one job is marked with `+`, and another is marked with `-`.
> The `+` marks the default job selected when you run `fg` or `bg` without specifying job index.
> The `-` marks the job which becomes the default one when `+`-marked job is finished.

In BShell, the default job selected when you run `fg` without specifying job index is the last job which is `Running` or `Stopped`.
The default job selected when you run `bg` without specifying job index is the last job which is `Stopped`.
Owing to this behavior, BShell does not give the `+` and `-` marks to jobs.
I think this behavior is intuitive enough.

```
$ sleep 10 &
  => prints "[1] (PGID of the new process)"
```

```
$ sleep 1000000 &
  => prints "[1] (PGID of the new process)"
$ sleep 1100000 &
  => prints "[2] (PGID of the new process)"
$ sleep 1200000 &
  => prints "[3] (PGID of the new process)"
$ jobs
  => prints
  """
  [1]  Running                 sleep 1000000 &
  [2]  Running                 sleep 1100000 &
  [3]  Running                 sleep 1200000 &
  """
$ fg
  => bring `sleep 1200000` job to foreground
     if you type `Ctrl+C`, `sleep 1200000` is killed.
     if you type `Ctrl+Z`, `sleep 1200000` is stopped and prints
  """
  [3]  Stopped                 sleep 1200000 &
  """
```

```
$ sleep 1000000 &
$ sleep 1100000 &
$ sleep 1200000 &
$ jobs
  => prints
  """
  [1]  Running                 sleep 1000000 &
  [2]  Running                 sleep 1100000 &
  [3]  Running                 sleep 1200000 &
  """
$ fg 2
  => bring `sleep 1100000` job to foreground
$ Ctrl+Z
  => prints "[2]  Stopped                 sleep 1100000 &"
$ jobs
  => prints
  """
  [1]  Running                 sleep 1000000 &
  [2]  Stopped                 sleep 1100000 &
  [3]  Running                 sleep 1200000 &
  """
$ bg
  => prints "[2]  sleep 1100000 &"
$ jobs
  => prints
  """
  [1]  Running                 sleep 1000000 &
  [2]  Running                 sleep 1100000 &
  [3]  Running                 sleep 1200000 &
  """
```

# Structure

## Input handling and Builtin function calls

The entry point of BShell is largely influenced by the following article.

https://brennan.io/2015/01/16/write-a-shell-in-c/

`readline` is quite a common and ordinary function that reads user input.
I used the `Ish_read_line` function in the article with almost no modification because I found it difficult to make _My Original_ implementation.

The idea of using function pointers to implement the builtin commands is also from the article.

## Lexer & Parser

I implemented a simple lexical analyzer and recursive decent parser (or top-down parser).

The BNF of the accepted language is as follows.

```
<expr> := <postfixed-expr>+
<postfixed-expr> := <fore-expr> `&`
<fore-expr> := <exec-unit> (`|` <exec-unit> | `>` <exec-unit>)*
<exec-unit> := `word`+
```

I did not take a look at the official shellscript grammar. I built up the BNF of the shellscript subset language from my observations of how bash behaves.

This language is simple enough to implement by `split` functions, but I decided to build the AST(Abstract Syntax Tree) in cosideration of future extensability and ease of debugging.

## Evaluation of AST

### Simple Command

Simple commands like `$ cat bshell.c` are implemented by making a `execve` system call.
For convenience, I used `execvp` to avoid problems regarding PATH.

Since `execve` system call replaces the current process with the new `execve` process, I made `fork` system call to keep the shell process alive.
Parent process make `waitpid` system call to wait for the finish of the child `execve` process.

### Piping

AST is a recursive data structure, so a recursive function can be a suitable option for the evaluation of AST.
The node of AST that holds piping info is a binary node.
The result of the left-child execution shall be _piped_ to the right-child execution.

To be more specific, STDOUT of left-child process shall be _piped_ to the STDIN of the right-child process.
I implemented piping by making `pipe` and `dup` system calls. I took a considerable amout of care of the open/close of the pipes.

### Input/Output Redirecting

Redirecting is quite similar to piping in that its main concern is where STDIN/STDOUT should be directed.
I could use `dup` system call, but I made use of `close` and `open` system calls, with which the code becomes easier to read for me.

I made two bool-typed global variables `is_in_redirecting` and `is_out_redirecting` to handle the multiple redirections like `$ ls > a.txt > b.txt > c.txt`

### Job Cotrol

#### Data Structure

Job control was a completely new idea for me, so I used the following GNU document as reference.

https://www.gnu.org/software/libc/manual/html_node/Implementing-a-Shell.html

As in the document, job data structure is implemented as a linked list.

The main difference with GNU document is that BShell jobs do hold PGID, but not the PIDs of the process group.
Thus, if piping is used with processes that consumes time after it finishes STDOUT output as shown in `$ sleep 5 | sleep 1`,
the job is reported to be finished once the process-group leader `sleep 1` gets finished. Note that the process `sleep 5` is still alive in background.

> In bash, the execution of `$ sleep 5 | sleep 1` waits for 5 seconds

I gave up on implementing this bash-behavior because I implemented piping as a recursive process generation in child processes and found no means to send the child process PIDs to the parent process.

#### Job Status Reporting

Each job has a state which is `Running`, `Stopped`, or `Finished` as mentioned in the _Usage_.

In the input-prompting infinite loop, right after user type _Enter_,
BShell checks whether each `Running` or `Stopped` job should be marked `Finished`.
When it finds newly `Finished` job, it reports the finished jobs to the user by printing the information.

This checking is implemented by making `waitpid` system calls with `WNOHANG` argument.

#### `jobs` Builtin Command

The implementation of `jobs` is almost the same as the job status reporting. `jobs` prints the detailed information.

#### `fg` Builtin Command

You can make `tcsetpgrp` system call to make a process group foreground.

In essence, `fg` implementation is as simple as making this system call, but I stumbled upon several points.

-   `tcsetpgrp` sends `SIGTTOU` and `SIGTTIN` signals, so BShell needs to ignore these two signals during the execution of `fg` command
-   BShell needs to send `SIGCONT` signal to the process in case it is not running
-   The PPID of the process needs to be the BShell PID

#### `bg` Builtin Command

`bg` is implemented just by sending `SIGCONT` signal to a process group. It also marks the job `Running`.
