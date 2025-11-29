#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

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
                            *start == '\r' || *start == '\f' || *start == '\v')) {
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

static int builtin_echo(int argc, char* argv[]) {
  int newline = 1;
  int i = 1;

  if (i < argc && strcmp(argv[i], "-n") == 0) {
    newline = 0;
    i++;
  }

  int first = 1;
  for (; i < argc; ++i) {
    if (!first) {
      putchar(' ');
    }
    first = 0;
    fputs(argv[i], stdout);
  }

  if (newline) {
    putchar('\n');
  }
  fflush(stdout);
  return 0;
}

static int builtin_pwd(int argc, char* argv[]) {
  optind = 1;
  opterr = 0;
  int opt;

  while ((opt = getopt(argc, argv, "LP")) != -1) {
    switch (opt) {
      case 'L':
      case 'P':
        break;
      default:
        fprintf(stderr, "pwd: unsupported option '-%c'\n", optopt);
        return 1;
    }
  }

  if (optind < argc) {
    fprintf(stderr, "pwd: too many arguments\n");
    return 1;
  }

  char buf[4096];
  if (!getcwd(buf, sizeof(buf))) {
    perror("pwd");
    return 1;
  }
  printf("%s\n", buf);
  fflush(stdout);
  return 0;
}

static int cmp_str_ptr(const void* a, const void* b) {
  const char* const* sa = (const char* const*)a;
  const char* const* sb = (const char* const*)b;
  return strcmp(*sa, *sb);
}

static int builtin_ls(int argc, char* argv[]) {
  int show_all = 0;
  const char* path = ".";

  optind = 1;
  opterr = 0;
  int opt;

  while ((opt = getopt(argc, argv, "a")) != -1) {
    switch (opt) {
      case 'a':
        show_all = 1;
        break;
      default:
        fprintf(stderr, "ls: unsupported option '-%c'\n", optopt);
        return 1;
    }
  }

  if (optind < argc) {
    if (argc - optind > 1) {
      fprintf(stderr, "ls: multiple paths are not supported\n");
      return 1;
    }
    path = argv[optind];
  }

  DIR* d = opendir(path);
  if (!d) {
    fprintf(stderr, "ls: cannot open '%s': %s\n", path, strerror(errno));
    return 1;
  }

  size_t cap = 64;
  size_t n = 0;
  char** names = (char**)malloc(cap * sizeof(char*));
  if (!names) {
    fprintf(stderr, "ls: out of memory\n");
    closedir(d);
    return 1;
  }

  struct dirent* de;
  while ((de = readdir(d)) != NULL) {
    if (!show_all && de->d_name[0] == '.') {
      continue;
    }
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
      continue;
    }

    if (n == cap) {
      cap *= 2;
      char** nn = (char**)realloc(names, cap * sizeof(char*));
      if (!nn) {
        fprintf(stderr, "ls: out of memory\n");
        for (size_t i = 0; i < n; ++i) {
          free(names[i]);
        }
        free(names);
        closedir(d);
        return 1;
      }
      names = nn;
    }

    names[n] = strdup(de->d_name);
    if (!names[n]) {
      fprintf(stderr, "ls: out of memory\n");
      for (size_t i = 0; i < n; ++i) {
        free(names[i]);
      }
      free(names);
      closedir(d);
      return 1;
    }
    n++;
  }
  closedir(d);

  qsort(names, n, sizeof(char*), cmp_str_ptr);

  for (size_t i = 0; i < n; ++i) {
    printf("%s\n", names[i]);
    free(names[i]);
  }
  free(names);
  fflush(stdout);
  return 0;
}

static int builtin_touch(int argc, char* argv[]) {
  int no_create = 0;

  optind = 1;
  opterr = 0;
  int opt;

  while ((opt = getopt(argc, argv, "c")) != -1) {
    switch (opt) {
      case 'c':
        no_create = 1;
        break;
      default:
        fprintf(stderr, "touch: unsupported option '-%c'\n", optopt);
        return 1;
    }
  }

  if (optind >= argc) {
    return 0;
  }

  int rc = 0;
  for (int i = optind; i < argc; ++i) {
    const char* path = argv[i];

    int fd = open(path, no_create ? O_WRONLY : (O_CREAT | O_WRONLY), 0666);
    if (fd < 0) {
      if (no_create && errno == ENOENT) {
        continue;
      }
      fprintf(stderr, "touch: cannot touch '%s': %s\n", path, strerror(errno));
      rc = 1;
      continue;
    }
    close(fd);

    struct utimbuf tb;
    tb.actime = tb.modtime = time(NULL);
    if (utime(path, &tb) != 0) {
      fprintf(stderr, "touch: utime failed for '%s': %s\n",
              path, strerror(errno));
      rc = 1;
    }
  }
  return rc;
}

static int builtin_rm(int argc, char* argv[]) {
  int force = 0;

  optind = 1;
  opterr = 0;
  int opt;

  while ((opt = getopt(argc, argv, "f")) != -1) {
    switch (opt) {
      case 'f':
        force = 1;
        break;
      default:
        fprintf(stderr, "rm: unsupported option '-%c'\n", optopt);
        return 1;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "rm: missing operand\n");
    return 1;
  }

  int rc = 0;
  for (int i = optind; i < argc; ++i) {
    const char* path = argv[i];
    if (unlink(path) != 0) {
      if (force && errno == ENOENT) {
        continue;
      }
      fprintf(stderr, "rm: cannot remove '%s': %s\n", path, strerror(errno));
      rc = 1;
    }
  }
  return rc;
}

static int builtin_wc(int argc, char* argv[]) {
  optind = 1;
  opterr = 0;
  int opt;
  int count_bytes = 0;

  while ((opt = getopt(argc, argv, "c")) != -1) {
    switch (opt) {
      case 'c':
        count_bytes = 1;
        break;
      default:
        fprintf(stderr, "wc: unsupported option '-%c'\n", optopt);
        return 1;
    }
  }

  if (!count_bytes || optind != argc - 1) {
    fprintf(stderr, "wc: only 'wc -c FILE' is supported\n");
    return 1;
  }

  const char* path = argv[optind];
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "wc: cannot open '%s': %s\n", path, strerror(errno));
    return 1;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    fprintf(stderr, "wc: fstat failed for '%s': %s\n", path, strerror(errno));
    close(fd);
    return 1;
  }
  close(fd);

  printf("%lld %s\n", (long long)st.st_size, path);
  fflush(stdout);
  return 0;
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

  if (strcmp(argv[0], "echo") == 0) {
    int rc = builtin_echo(argc, argv);
    double end = now_seconds();
    fprintf(stderr, "Command executed in %.6f seconds\n", end - start);
    fflush(stderr);
    return rc;
  }

  if (strcmp(argv[0], "pwd") == 0) {
    int rc = builtin_pwd(argc, argv);
    double end = now_seconds();
    fprintf(stderr, "Command executed in %.6f seconds\n", end - start);
    fflush(stderr);
    return rc;
  }

  if (strcmp(argv[0], "ls") == 0) {
    int rc = builtin_ls(argc, argv);
    double end = now_seconds();
    fprintf(stderr, "Command executed in %.6f seconds\n", end - start);
    fflush(stderr);
    return rc;
  }

  if (strcmp(argv[0], "touch") == 0) {
    int rc = builtin_touch(argc, argv);
    double end = now_seconds();
    fprintf(stderr, "Command executed in %.6f seconds\n", end - start);
    fflush(stderr);
    return rc;
  }

  if (strcmp(argv[0], "rm") == 0) {
    int rc = builtin_rm(argc, argv);
    double end = now_seconds();
    fprintf(stderr, "Command executed in %.6f seconds\n", end - start);
    fflush(stderr);
    return rc;
  }

  if (strcmp(argv[0], "wc") == 0) {
    int rc = builtin_wc(argc, argv);
    double end = now_seconds();
    fprintf(stderr, "Command executed in %.6f seconds\n", end - start);
    fflush(stderr);
    return rc;
  }

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
  fprintf(stderr, "Command executed in %.6f seconds\n", end - start);
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