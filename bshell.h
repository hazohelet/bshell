#ifndef __BSHELL_H__
#define __BSHELL_H__

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGC 32

typedef struct Job {
  struct Job *next;
  pid_t pgid;
  int num; // job number specified in fg
  bool running;
  bool suspended;
  char *command;
} Job;

Job *make_job(pid_t pgid, char *command, bool running);
Job *get_last_unfinished_job();
Job *get_last_suspended_job();
Job *find_job_by_num(int num);
void update_job_status(bool show_all);
void delete_job_by_pid(pid_t pid);
void bring_job_to_foreground(Job *job);

void init_signals();
void *bshell_malloc(size_t size);

typedef enum {
  TOKEN_WORD,
  TOKEN_RESERVED,
  TOKEN_PUNCTUATOR,
} TokenKind;

typedef struct Token Token;

struct Token {
  TokenKind kind;
  char *loc;
  Token *next;
};

void tokenize(char *input);
void dump_tokens(Token *tokens);

typedef struct ExecutableUnit ExecutableUnit;
struct ExecutableUnit {
  int argc;
  Token **argv_tokens;
};

typedef enum {
  NODE_PIPE,
  NODE_OUTPUT_REDIRECT,
  NODE_INPUT_REDIRECT,
  NODE_EXECUNIT,
  NODE_SEQUENCE,
  NODE_BACKGROUND_EXEC
} ASTNodeKind;

typedef struct ASTNode ASTNode;
struct ASTNode {
  ASTNodeKind kind;          // indicate what operation needs to be performed
  ASTNode *lhs, *rhs;        // for binary and unary operations
  Token *tok;                // representative token
  ExecutableUnit *exec_unit; // executable unit for leef node
};

ASTNode *expr();
void dump_tree(ASTNode *node);
char *get_command(ASTNode *node);
void free_ast(ASTNode *node);
void free_tokens(Token *tok);
void evaluate_ast(ASTNode *ast, bool make_childprocess);

int bsh_cd(char **);
int bsh_help(char **);
int bsh_exit(char **);
int bsh_bg(char **);
int bsh_fg(char **);
int bsh_jobs(char **);
int launch_builtin_command_if_possible(char **argv);

#endif