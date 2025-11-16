#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vtsh.h>

#define MAX_LINE 4096
#define MAX_ARGS 128

static double now_seconds(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static void trim_whitespace(char* s) {
  if (!s) {
    return;
  }

  char* start = s;
  while (*start != '\0' && (*start == ' ' || *start == '\t' || *start == '\n' ||
                            *start == '\r' || *start == '\f' || *start == '\v')
  ) {
    start++;
  }
  if (start != s) {
    memmove(s, start, strlen(start) + 1);
  }

  size_t len = strlen(s);
  while (len > 0) {
    char c = s[len - 1];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
        c == '\v') {
      s[len - 1] = '\0';
      len--;
    } else {
      break;
    }
  }
}

static int run_simple_command(char* cmd) {
  trim_whitespace(cmd);
  if (*cmd == '\0') {
    return 0;
  }

  char* argv[MAX_ARGS];
  int argc = 0;
  char* token = strtok(cmd, " \t");
  while (token != NULL && argc < MAX_ARGS - 1) {
    argv[argc++] = token;
    token = strtok(NULL, " \t");
  }
  argv[argc] = NULL;

  if (argc == 0) {
    return 0;
  }

  if (strcmp(argv[0], "exit") == 0) {
    exit(0);
  }

  if (strcmp(argv[0], "./shell") == 0) {
    return 0;
  }

  if (strcmp(argv[0], "cat") == 0 && argc == 1) {
    int ch;
    while ((ch = getchar()) != EOF) {
      if (putchar(ch) == EOF) {
        break;
      }
    }
    fflush(stdout);
    return 0;
  }

  double start = now_seconds();

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 0;
  }

  if (pid == 0) {
    execvp(argv[0], argv);

    printf("Command not found\n");
    fflush(stdout);
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    perror("waitpid");
    return 0;
  }

  double end = now_seconds();
  double elapsed = end - start;
  fprintf(stderr, "Command executed in %.6f seconds\n", elapsed);
  fflush(stderr);

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 0;
}

static void run_line(char* line) {
  size_t len = strlen(line);
  if (len > 0 && line[len - 1] == '\n') {
    line[len - 1] = '\0';
  }

  char* cursor = line;
  bool execute_chain = true;

  while (cursor != NULL) {
    char* segment = cursor;
    char* op = strstr(cursor, "&&");
    if (op != NULL) {
      *op = '\0';
      cursor = op + 2;
    } else {
      cursor = NULL;
    }

    trim_whitespace(segment);
    if (*segment == '\0') {
      continue;
    }

    if (!execute_chain) {
      continue;
    }

    int status = run_simple_command(segment);
    if (status != 0) {
      execute_chain = false;
    }
  }
}

int main(void) {
  char line[MAX_LINE];

  while (1) {
    printf("%s", vtsh_prompt());
    fflush(stdout);

    if (fgets(line, sizeof(line), stdin) == NULL) {
      break;
    }

    char tmp[MAX_LINE];
    strncpy(tmp, line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    trim_whitespace(tmp);
    if (*tmp == '\0') {
      continue;
    }

    run_line(line);
  }

  return 0;
}