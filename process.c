#include "bshell.h"

Job *first_job;

Job *find_job_place() {
  Job *job = first_job;
  if (!job) {
    Job *job = bshell_malloc(sizeof(Job));
    job->num = 1;
    job->next = NULL;
    first_job = job;
    return first_job;
  }

  int num = 1;
  Job *prev;
  while (job && (job->running || job->suspended)) {
    prev = job;
    job = job->next;
    ++num;
  }
  if (!job) { // if all jobs are unnotified (running)
    Job *new_job = bshell_malloc(sizeof(Job));
    new_job->num = num;
    new_job->next = NULL;
    prev->next = new_job;
    return new_job;
  }
  // this is when we find a job finished
  return job;
}

Job *make_job(pid_t pgid, char *command, bool running) {
  Job *job = find_job_place();
  job->pgid = pgid;
  job->command = command;
  if (running) {
    job->running = true;
    job->suspended = false;
  } else {
    job->running = false;
    job->suspended = true;
  }
  return job;
}

Job *find_job_by_num(int num) {
  Job *cur = first_job;
  while (cur) {
    if (cur->num == num)
      return cur;
    cur = cur->next;
  }
  return NULL;
}

Job *get_last_unfinished_job() {
  Job *cur = first_job;
  Job *cand = NULL;
  while (cur) {
    if (cur->running || cur->suspended)
      cand = cur;
    cur = cur->next;
  }
  return cand;
}

Job *get_last_suspended_job() {
  Job *cur = first_job;
  Job *cand = NULL;
  while (cur) {
    if (cur->suspended)
      cand = cur;
    cur = cur->next;
  }
  return cand;
}

void delete_job_by_pid(pid_t pid) { // return next job
  Job *cur = first_job;
  while (cur) {
    if (cur->pgid == pid) {
      cur->running = false;
      cur->suspended = false;
      free(cur->command);
      return;
    }
    cur = cur->next;
  }
  printf("bshell: job not found: %d\n", pid);
  return; // unreachable
}

void update_job_status(bool show_all) {
  if (!first_job)
    return;

  Job *cur = first_job;
  while (cur) {
    int status;
    if (!cur->running && !cur->suspended) {
      cur = cur->next;
      continue;
    }
    pid_t pid = waitpid(cur->pgid, &status, WNOHANG);
    if (pid == 0) {
      if (show_all && cur->running)
        fprintf(stderr, "[%d]  Running                 %s\n", cur->num,
                cur->command);
      else if (show_all && cur->suspended)
        fprintf(stderr, "[%d]  Stopped                 %s\n", cur->num,
                cur->command);
    } else {
      fprintf(stderr, "[%d]  Done                    %s\n", cur->num,
              cur->command);
      delete_job_by_pid(pid);
      continue;
    }
    cur = cur->next;
  }
}

void bring_job_to_foreground(Job *job) {
  pid_t pgid = job->pgid;

  printf("%s\n", job->command);

  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);

  if (tcsetpgrp(STDIN_FILENO, pgid) < 0) {
    perror("tcsetpgrp");
  }
  kill(-pgid, SIGCONT);

  int status;
  waitpid(pgid, &status, WUNTRACED);
  if (WIFSTOPPED(status)) {
    fprintf(stderr, "\n[%d]  Stopped                 %s\n", job->num,
            job->command);
    job->suspended = true;
    job->running = false;
  } else {
    delete_job_by_pid(pgid);
  }

  if (tcsetpgrp(STDIN_FILENO, getpgrp()) < 0) {
    perror("tcsetpgrp");
  }

  signal(SIGTTIN, SIG_DFL);
  signal(SIGTTOU, SIG_DFL);
}