#include "bshell.h"

#define READLINE_BUFSIZE 1024

extern Token *cur;
extern bool syntax_error;
pid_t shell_pgid;

void *bshell_malloc(size_t size) {
  void *ptr = malloc(size);
  if (!ptr) {
    fprintf(stderr, "bshell: allocation error");
    exit(EXIT_FAILURE);
  }
  return ptr;
}

char *bshell_readline() {
  int pos = 0;
  size_t bufsize = READLINE_BUFSIZE;
  char *buffer = (char *)bshell_malloc(bufsize * sizeof(char));
  int c;

  while (1) {
    c = getchar();

    if (c == '\n') {
      buffer[pos] = '\0';
      return buffer;
    } else if (c == EOF) {
      printf("\n");
      exit(-1);
    } else {
      buffer[pos] = c;
    }
    pos++;

    if (pos > (int)bufsize) {
      bufsize += READLINE_BUFSIZE;
      buffer = (char *)realloc(buffer, bufsize);
      if (!buffer) {
        fprintf(stderr, "bshell: allocation error");
        exit(EXIT_FAILURE);
      }
    }
  }
}

void bshell_init() {
  shell_pgid = getpid();
  if (setpgid(shell_pgid, shell_pgid) < 0) {
    perror("Couldn't put the shell in its own process group");
    exit(EXIT_FAILURE);
  }
  tcsetpgrp(STDIN_FILENO, shell_pgid);
}

int main() {
  bshell_init();
  do {
    fprintf(stderr, "$ ");

    char *line = bshell_readline();
    update_job_status(false);

    tokenize(line);
    Token *head = cur;

    if (head) {
      ASTNode *root = expr();
      if (syntax_error) {
        syntax_error = false;
      } else {
        evaluate_ast(root, true);
      }
      free_ast(root);
      free_tokens(head);
    }
    free(line);
  } while (1);

  return 0;
}