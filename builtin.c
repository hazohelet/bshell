#include "bshell.h"

extern pid_t shell_pgid;
extern Job *first_job;

int bsh_cd(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "bshell: `cd` requires an argument");
  } else {
    if (chdir(args[1]) != 0) {
      perror("bshell");
    }
  }
  return 1;
}

int bsh_exit(char **args) { exit(EXIT_SUCCESS); }

int bsh_help(char **args) {
  printf("This is BShell. A simple bash-like shell written in C.\nAs of now, "
         "Pipe, Output redirection, and several builtins including `fg`, `bg`, "
         "`jobs` have been implemented.\n");
  return 1;
}

int bsh_bg(char **args) {
  Job *job;
  if (!args[1]) { // if no arg is specified, bg the last job
    job = get_last_suspended_job();
    if (!job) {
      fprintf(stderr, "bshell: bg: current: no such job\n");
      return 1;
    }
  } else {
    int num = atoi(args[1]);
    job = find_job_by_num(num);
    if (!job || (!job->suspended && !job->running)) {
      fprintf(stderr, "bshell: bg: %d: no such job\n", num);
      return 1;
    } else if (job->running) {
      fprintf(stderr, "bshell: bg: job %d already in background\n", num);
      return 1;
    }
  }

  pid_t pgid = job->pgid;
  fprintf(stderr, "[%d]  %s\n", job->num, job->command);
  kill(-pgid, SIGCONT);
  job->suspended = false;
  job->running = true;

  return 1;
}

int bsh_fg(char **args) {
  Job *job;
  if (!args[1]) { // if no arg is specified, fg the last job
    job = get_last_unfinished_job();
    if (!job) {
      fprintf(stderr, "bshell: fg: current: no such job\n");
      return 1;
    }
  } else {
    int num = atoi(args[1]);
    job = find_job_by_num(num);
    if (!job || (!job->suspended && !job->running)) {
      fprintf(stderr, "bshell: fg: %d: no such job\n", num);
      return 1;
    }
  }

  bring_job_to_foreground(job);
  return 1;
}

int bsh_jobs(char **args) {
  update_job_status(true);
  return 1;
}

int launch_builtin_command_if_possible(char **argv) {
  char *builtin_str[] = {"cd", "help", "exit", "bg", "fg", "jobs"};
  int (*builtin_func[])(char **) = {&bsh_cd, &bsh_help, &bsh_exit,
                                    &bsh_bg, &bsh_fg,   &bsh_jobs};

  int builtin_count = sizeof(builtin_str) / sizeof(char *);

  for (int i = 0; i < builtin_count; i++) {
    if (strcmp(argv[0], builtin_str[i]) == 0) {
      return (*builtin_func[i])(argv);
    }
  }
  return -1;
}