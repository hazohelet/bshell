#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>

#define READLINE_BUFFSIZE 1024
#define CMD_TOKENS_BUFFSIZE 64
#define CMDS_GROUP_BUFFSIZE 32

/*
  prompt user input, read line, and return it
*/
char *bshell_readline() {
  printf(">> ");  // prompt input
  int    pos = 0;
  size_t bufsize = READLINE_BUFFSIZE;
  char  *buffer = (char *)malloc(bufsize * sizeof(char));
  int    c;

  if (!buffer) {
    fprintf(stderr, "bshell: allocation error");
    exit(EXIT_FAILURE);
  }

  while (1) {
    c = getchar();

    if (c == '\n') {  // if user press enter, stop the line reading
      buffer[pos] = '\0';
      return buffer;
    } else if (c ==
               EOF) {  // if user press Ctrl+D, exit the process immediately
      printf("\n");
      exit(-1);
    } else {  // otherwise, keep reading and saving the input
      buffer[pos] = c;
    }
    pos++;

    if (pos >= bufsize) {  // dynamically allocate memory if the buffer is full
      bufsize += READLINE_BUFFSIZE;
      buffer = (char *)realloc(buffer, bufsize);
      if (!buffer) {
        fprintf(stderr, "bshell: allocation error");
        exit(EXIT_FAILURE);
      }
    }
  }
}

char ***bshell_split_input(char *input) {  // simple 1-pass lexer and parser
  char   *cur = input;
  size_t  cmd_tokens_bufsize = CMD_TOKENS_BUFFSIZE;
  size_t  cmds_group_bufsize = CMDS_GROUP_BUFFSIZE;
  char ***cmds = (char ***)malloc(cmds_group_bufsize * sizeof(char **));
  int     cmds_pos = 0;
  char  **cmd_tokens = (char **)malloc(cmd_tokens_bufsize * sizeof(char *));
  int     cmd_tokens_pos = 0;
  char   *token;

  if (!cmds) {
    fprintf(stderr, "bshell: allocation error");
    exit(EXIT_FAILURE);
  }

  if (!cur) return NULL;  // if input is null, return immediately

  while (*cur) {
    if (isspace(*cur)) {  // skip whitespaces
      cur++;
    } else if (*cur == '|') {  // encounter piping, save the current command
      cmd_tokens[cmd_tokens_pos] = NULL;  // for the `execvp` requirement
      cmds[cmds_pos] = cmd_tokens;
      cmds_pos++;
      cmd_tokens_bufsize = CMD_TOKENS_BUFFSIZE;
      cmd_tokens = (char **)malloc(cmd_tokens_bufsize * sizeof(char *));
      cmd_tokens_pos = 0;
      cur++;
    } else if (isalnum(*cur) ||
               strchr("./-*&", *cur) != NULL) {  // normal token
      char *start = cur;
      cur++;
      while (*cur && !isspace(*cur)) {
        cur++;
      }
      token = (char *)malloc((cur - start + 1) * sizeof(char));
      memcpy(token, start, cur - start);
      token[cur - start] = '\0';
      cmd_tokens[cmd_tokens_pos] = token;
      cmd_tokens_pos++;
    } else if (*cur == '"') {  // string literal token
      char *start = ++cur;
      while (*cur && *cur != '"') {
        cur++;
      }
      token = (char *)malloc((cur - start + 1) * sizeof(char));
      memcpy(token, start, cur - start);
      token[cur - start] = '\0';
      cmd_tokens[cmd_tokens_pos] = token;
      cmd_tokens_pos++;
      cur++;
    } else {
      fprintf(stderr, "bshell: syntax error\n");
      fprintf(stderr, "        invalid character: %c\n", *cur);
      exit(EXIT_FAILURE);
    }
    if (cmds_pos >=
        cmds_group_bufsize - 1) {  // if cmds buffer is full, add more memory
      cmds_group_bufsize += CMDS_GROUP_BUFFSIZE;
      cmds = (char ***)realloc(cmds, cmds_group_bufsize * sizeof(char **));
      if (!cmds) {
        fprintf(stderr, "bshell: allocation error");
        exit(EXIT_FAILURE);
      }
    }
    if (cmd_tokens_pos >=
        cmd_tokens_bufsize - 1) {  // if cmd buffer is full, add more memory
      cmd_tokens_bufsize += CMD_TOKENS_BUFFSIZE;
      cmd_tokens =
          (char **)realloc(cmd_tokens, cmd_tokens_bufsize * sizeof(char *));

      if (!cmd_tokens) {
        fprintf(stderr, "bshell: allocation error");
        exit(EXIT_FAILURE);
      }
    }
  }
  cmd_tokens[cmd_tokens_pos] = NULL;
  cmds[cmds_pos] = cmd_tokens;
  return cmds;
}

void exec_cmd(char **cmd) {
  pid_t pid;
  int   status;

  pid = fork();
  if (pid == 0) {  // child process
    if (execvp(cmd[0], cmd) == -1) {
      perror("bshell");
    }
    exit(EXIT_SUCCESS);
  } else if (pid < 0) {  // forking error
    perror("bshell");
  } else {
    do {
      waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }
}

void launch_command(char **cmd) {
  const char *program = cmd[0];
  if (!program) {
    return;
  }
  if (strcmp(program, "exit") == 0) {
    exit(-1);
  }
  if (strcmp(program, "cd") == 0) {
    if (cmd[1] == NULL) {
      fprintf(stderr, "bshell: expected argument to \"cd\"\n");
      return;
    }
    if (chdir(cmd[1]) != 0) {
      perror("bshell");
    }
    return;
  }
  if (strcmp(program, "help") == 0) {
    printf("Bash-like shell implemented in C\n");
    printf("Type program names and arguments, and hit enter.\n");
    printf("The following are built in:\n");
    printf("  cd: change directory\n");
    printf("  help: display this help message\n");
    printf("  exit: exit the shell\n");
    return;
  }
  exec_cmd(cmd);
}

void use_pipe(char ***cmds, int cmd_num, int offset_from_last) {
  int    fd[2];
  int    status;
  pid_t  pid;
  char **cmd = cmds[cmd_num - offset_from_last - 1];

  if (offset_from_last == cmd_num - 1) {  // left end
    launch_command(cmd);
  } else {  // middle or right -> pipe and fork. parent exec and child
            // recurse
    pipe(fd);
    pid = fork();
    if (pid == 0) {  // child process
      close(fd[0]);
      dup2(fd[1], STDOUT_FILENO);
      close(fd[1]);

      use_pipe(cmds, cmd_num, offset_from_last + 1);
      exit(EXIT_SUCCESS);
    } else {  // parent process
      do {
        waitpid(pid, &status, WUNTRACED);
      } while (!WIFEXITED(status) && !WIFSIGNALED(status));
      close(fd[1]);
      dup2(fd[0], STDIN_FILENO);
      close(fd[0]);
      launch_command(cmd);
    }
  }
}

void clear_cmds(char ***cmds) {  // free all the memory
  char ***head = cmds;
  while (*cmds) {
    char **cmd = *cmds;
    while (*cmd) {
      free(*cmd);
      cmd++;
    }
    free(*cmds);
    cmds++;
  }
  free(head);
}

int main() {
  int status;
  do {
    char   *input = bshell_readline();
    char ***cmds = bshell_split_input(input);

    char ***p = cmds;
    int     cmd_num = 0;
    while (*p) {
      cmd_num++;
      p++;
    }

    if (cmd_num == 1) {  // no pipe
      launch_command(cmds[0]);
    } else {  // has piping
      pid_t pid = fork();
      if (pid == 0) {
        use_pipe(cmds, cmd_num, 0);
        exit(EXIT_FAILURE);
      } else {
        do {
          waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        // free in parent process only to avoid double free
        free(input);
        clear_cmds(cmds);
      }
    }
  } while (WEXITSTATUS(status) != (unsigned char)-1);
  exit(0);
}
