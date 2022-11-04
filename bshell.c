#include "bshell.h"

#define TOKEN_BUFSIZE 64

Token *cur;
bool is_out_redirecting = false; // if is in `>`
bool is_in_redirecting = false;  // if is in `<`
bool syntax_error = false;       // syntax error found in AST construction
extern Job *first_job;           // the job list
extern pid_t shell_pgid;         // PGID of the bshell process

static Token *new_token(TokenKind kind, char *start, int len) {
  Token *token = (Token *)bshell_malloc(sizeof(Token));
  char *loc = (char *)bshell_malloc(sizeof(char) * (len + 1));
  memcpy(loc, start, len);
  loc[len] = '\0';

  token->kind = kind;
  token->loc = loc;
  token->next = NULL;

  return token;
}

static bool startswith(char *str, char *prefix) {
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

static int read_punct(char *p) {
  char *puncts[] = {"&&", "||", ">>", "<<", "&", "|", ">", "<", ";", "(", ")"};
  for (unsigned int i = 0; i < sizeof(puncts) / sizeof(puncts[0]); i++) {
    if (startswith(p, puncts[i])) {
      return strlen(puncts[i]);
    }
  }
  return 0;
}

static int read_word(char *p) {
  int len = 0;
  for (; *p; p++, len++) {
    if (isspace(*p) || read_punct(p)) {
      return len;
    }
  }
  return len;
}

void tokenize(char *input) {
  Token head = {};
  Token *last_tok = &head;
  char *c = input;
  while (*c) {
    if (isspace(*c))
      c++;
    int punct_len = read_punct(c);
    if (punct_len > 0) {
      last_tok = last_tok->next = new_token(TOKEN_PUNCTUATOR, c, punct_len);
      c += punct_len;
      continue;
    }
    int word_len = read_word(c);
    if (word_len > 0) {
      last_tok = last_tok->next = new_token(TOKEN_WORD, c, word_len);
      c += word_len;
      continue;
    }
    c++;
  }
  cur = head.next;
}

void dump_tokens(Token *tokens) {
  if (!tokens) {
    printf("NULL\n");
    return;
  }
  Token *tok = tokens;
  do {
    fprintf(stderr, "%s(%d)-> ", tok->loc, tok->kind);
    tok = tok->next;
  } while (tok);
  fprintf(stderr, "NULL\n");
}

ASTNode *new_node(ASTNodeKind kind, Token *token, ASTNode *lhs, ASTNode *rhs,
                  ExecutableUnit *exec_unit) {
  ASTNode *node = (ASTNode *)bshell_malloc(sizeof(ASTNode));
  node->kind = kind;
  node->tok = token;
  node->lhs = lhs;
  node->rhs = rhs;
  node->exec_unit = exec_unit;

  return node;
}

static ASTNode *create_binary_node(ASTNodeKind kind, Token *token, ASTNode *lhs,
                                   ASTNode *rhs) {
  return new_node(kind, token, lhs, rhs, NULL);
}

static ASTNode *create_exec_node(Token *token, ExecutableUnit *exec_unit) {
  return new_node(NODE_EXECUNIT, token, NULL, NULL, exec_unit);
}

/*
BNF of accepted input:
  <expr> := <postfixed-expr>+
  <postfixed-expr> := <fore-expr> `&`
  <fore-expr> := <exec-unit> (`|` <exec-unit> | `>` <exec-unit>)*
  <exec-unit> := `word`+
*/

static bool expect(TokenKind kind) {
  if (!cur || cur->kind != kind) {
    syntax_error = true;
    return false;
  }
  return true;
}
static bool consume(TokenKind kind) {
  if (!cur || cur->kind != kind) {
    return false;
  }
  cur = cur->next;
  return true;
}

static bool consume_punct(char *punct) {
  int tok_len = strlen(punct);
  if (cur && (size_t)tok_len == strlen(cur->loc) &&
      strncmp(cur->loc, punct, tok_len) == 0) {
    cur = cur->next;
    return true;
  }
  return false;
}

static ASTNode *exec_unit() {
  Token *start = cur;
  if (!expect(TOKEN_WORD)) {
    fprintf(stderr, "Unexpected token or EOF\n");
    return NULL;
  }
  ExecutableUnit *exec_unit = bshell_malloc(sizeof(ExecutableUnit));

  exec_unit->argv_tokens = bshell_malloc(MAX_ARGC * sizeof(Token *));
  exec_unit->argc = 0;
  for (; cur;) {
    Token *tok = cur;
    if (consume(TOKEN_WORD)) {
      exec_unit->argv_tokens[exec_unit->argc++] = tok;
      continue;
    }
    break;
  }
  exec_unit->argv_tokens[exec_unit->argc] = NULL;
  return create_exec_node(start, exec_unit);
}

static ASTNode *fore_expr() {
  ASTNode *ast = exec_unit();
  if (!ast) {
    return NULL;
  }
  for (; cur;) {
    if (consume_punct("|")) {
      ast = create_binary_node(NODE_PIPE, cur, ast, exec_unit());
      continue;
    }
    if (consume_punct(">")) {
      ast = create_binary_node(NODE_OUTPUT_REDIRECT, cur, ast, exec_unit());
      continue;
    }
    if (consume_punct("<")) {
      ast = create_binary_node(NODE_INPUT_REDIRECT, cur, ast, exec_unit());
      continue;
    }
    break;
  }
  return ast;
}

static ASTNode *postfixed_expr() {
  ASTNode *ast = fore_expr();
  if (!ast) {
    return NULL;
  }
  if (consume_punct("&"))
    ast = create_binary_node(NODE_BACKGROUND_EXEC, cur, ast, NULL);
  return ast;
}

ASTNode *expr() {
  ASTNode *ast = postfixed_expr();
  if (!ast) {
    return NULL;
  }
  for (; cur;) {
    if (cur->kind == TOKEN_WORD) {
      ast = create_binary_node(NODE_SEQUENCE, cur, ast, postfixed_expr());
      continue;
    }
    fprintf(stderr, "Unexpected token: %s\n", cur->loc);
    syntax_error = true;
    break;
  }
  return ast;
}

void dump_tree(ASTNode *ast) {
  char *str = get_command(ast);
  fprintf(stderr, "%s\n", str);
}

char *get_command(ASTNode *ast) {
  if (!ast) {
    return NULL;
  }
  char *command;
  size_t len;
  FILE *stream = open_memstream(&command, &len);
  char *lhs = NULL;
  char *rhs = NULL;

  switch (ast->kind) {
  case NODE_EXECUNIT: {
    Token *tok = ast->tok;
    while (tok && tok->kind == TOKEN_WORD) {
      fprintf(stream, "%s", tok->loc);
      if (tok->next && tok->next->kind == TOKEN_WORD)
        fprintf(stream, " ");
      tok = tok->next;
    }
  } break;
  case NODE_PIPE: {
    lhs = get_command(ast->lhs);
    rhs = get_command(ast->rhs);
    fprintf(stream, "%s | %s", lhs, rhs);
  } break;
  case NODE_OUTPUT_REDIRECT: {
    lhs = get_command(ast->lhs);
    rhs = get_command(ast->rhs);
    fprintf(stream, "%s > %s", lhs, rhs);
  } break;
  case NODE_INPUT_REDIRECT: {
    lhs = get_command(ast->lhs);
    rhs = get_command(ast->rhs);
    fprintf(stream, "%s < %s", lhs, rhs);
  } break;
  case NODE_BACKGROUND_EXEC: {
    lhs = get_command(ast->lhs);
    fprintf(stream, "%s &", lhs);
  } break;
  case NODE_SEQUENCE: {
    lhs = get_command(ast->lhs);
    rhs = get_command(ast->rhs);
    fprintf(stream, "%s; %s", lhs, rhs);
  } break;
  }
  fclose(stream);
  if (lhs)
    free(lhs);
  if (rhs)
    free(rhs);
  return command;
}

static char **make_argv_from_exec_unit(ASTNode *node) {
  ExecutableUnit *exec_unit = node->exec_unit;
  Token **argv_tokens = exec_unit->argv_tokens;
  int argc = exec_unit->argc;

  char **argv = (char **)bshell_malloc(sizeof(char *) * (argc + 1));

  for (int i = 0; i < argc; i++) {
    Token *tok = argv_tokens[i];
    argv[i] = tok->loc;
  }

  argv[argc] = NULL;

  return argv;
}

static void launch_exec_unit(ASTNode *node, bool make_childprocess) {

  char **argv = make_argv_from_exec_unit(node);

  int builtin_trial = launch_builtin_command_if_possible(argv);
  if (builtin_trial != -1) {
    return;
  }

  if (!make_childprocess) {
    if (execvp(argv[0], argv) == -1) {
      perror("bshell");
      exit(EXIT_FAILURE);
    }
    return;
  }

  pid_t pid;
  int status;

  pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    if (execvp(argv[0], argv) == -1) {
      perror("bshell");
      exit(EXIT_FAILURE);
    }
  } else if (pid < 0) {
    perror("bshell");
  } else {
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    setpgid(pid, 0);
    tcsetpgrp(STDIN_FILENO, pid);
    waitpid(pid, &status, WUNTRACED);
    if (WIFSTOPPED(status)) {
      Job *job = make_job(pid, get_command(node), false);
      fprintf(stderr, "\n[%d]  Stopped                 %s\n", job->num,
              job->command);
    }
    tcsetpgrp(STDIN_FILENO, getpgrp());
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
  }
  return;
}

static void handle_pipe(ASTNode *ast) {
  ASTNode *lhs = ast->lhs;
  ASTNode *rhs = ast->rhs;

  int fd[2];
  pid_t pid;

  if (ast->kind != NODE_PIPE) {
    launch_exec_unit(ast, false);
    return;
  }

  pipe(fd);
  pid = fork();

  if (pid == 0) {
    dup2(fd[1], STDOUT_FILENO);
    close(fd[0]);
    close(fd[1]);
    evaluate_ast(lhs, false);
  } else if (pid < 0) {
    perror("bshell");
  } else {
    dup2(fd[0], STDIN_FILENO);
    close(fd[0]);
    close(fd[1]);
    signal(SIGCHLD, SIG_IGN);
    launch_exec_unit(rhs, false);
  }
  return;
}

static void handle_output_redirection(ASTNode *node) {
  ASTNode *lhs = node->lhs;
  ASTNode *rhs = node->rhs;

  char *filename = rhs->tok->loc;
  if (!is_out_redirecting) {
    is_out_redirecting = true;
    close(STDOUT_FILENO);
    open(filename, (O_WRONLY | O_CREAT | O_TRUNC), 0664);
  } else {
    FILE *fp = fopen(filename, "w");
    fclose(fp);
  }

  evaluate_ast(lhs, false);
  is_out_redirecting = false;
}

static void handle_input_redirection(ASTNode *node) {
  ASTNode *lhs = node->lhs;
  ASTNode *rhs = node->rhs;

  char *filename = rhs->tok->loc;
  if (!is_in_redirecting) {
    is_in_redirecting = true;
    close(STDIN_FILENO);
    if (open(filename, O_RDONLY, 0) < 0) {
      perror("bshell");
      exit(EXIT_FAILURE);
    };
  }
  evaluate_ast(lhs, false);
  is_in_redirecting = false;
}

void exec_background(ASTNode *node) {
  pid_t pid = fork();

  if (pid == 0) {
    setpgrp();
    evaluate_ast(node->lhs, false);
  } else if (pid < 0) {
    perror("bshell");
  } else {
    setpgrp();
    Job *job = make_job(pid, get_command(node), true);
    fprintf(stderr, "[%d] %d\n", job->num, pid);
    tcsetpgrp(STDIN_FILENO, getpgrp());
  }

  return;
}

void evaluate_ast(ASTNode *ast, bool make_childprocess) {
  if (!ast) {
    return;
  }
  if (make_childprocess) {
    if (ast->kind == NODE_PIPE || ast->kind == NODE_OUTPUT_REDIRECT ||
        ast->kind == NODE_INPUT_REDIRECT) {
      pid_t pid = fork();
      if (pid == 0) { // child process
        evaluate_ast(ast, false);
        exit(EXIT_SUCCESS);
      } else if (pid > 0) {
        int status;
        waitpid(pid, &status, WUNTRACED);
      } else {
        perror("bshell");
      }
      return;
    }
  }

  switch (ast->kind) {
  case NODE_PIPE: {
    handle_pipe(ast);
  } break;
  case NODE_OUTPUT_REDIRECT: {
    handle_output_redirection(ast);
  } break;
  case NODE_INPUT_REDIRECT: {
    handle_input_redirection(ast);
  } break;
  case NODE_EXECUNIT: {
    launch_exec_unit(ast, make_childprocess);
  } break;
  case NODE_BACKGROUND_EXEC: {
    exec_background(ast);
  } break;
  case NODE_SEQUENCE: {
    evaluate_ast(ast->lhs, make_childprocess);
    evaluate_ast(ast->rhs, make_childprocess);
  } break;
  }
}

void free_ast(ASTNode *ast) {
  if (!ast) {
    return;
  }
  free_ast(ast->lhs);
  free_ast(ast->rhs);
  free(ast);
}

void free_tokens(Token *tok) {
  if (!tok) {
    return;
  }
  free_tokens(tok->next);
  free(tok->loc);
  free(tok);
}