#ifdef __linux__

#define _GNU_SOURCE
#include <errno.h>
#include <linux/sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static double now_seconds(void) {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    return 0.0;
  }
  return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static pid_t clone3_fork_like(void) {
  struct clone_args args;
  memset(&args, 0, sizeof(args));
  args.exit_signal = SIGCHLD;

  long ret = syscall(SYS_clone3, &args, sizeof(args));
  if (ret == -1) {
    return -1;
  }
  return (pid_t)ret;
}

static void usage(const char* prog) {
  fprintf(stderr, "Usage: %s <program> [args...]\n", prog);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  double start = now_seconds();
  pid_t pid = clone3_fork_like();
  if (pid < 0) {
    fprintf(stderr, "proc-clone3: clone3 failed: %s\n", strerror(errno));
    return 1;
  }

  if (pid == 0) {
    // child
    execvp(argv[1], &argv[1]);
    fprintf(stderr, "proc-clone3: execvp failed: %s\n", strerror(errno));
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    fprintf(stderr, "proc-clone3: waitpid failed: %s\n", strerror(errno));
    return 1;
  }

  double end = now_seconds();

  printf("proc-clone3: child pid %d\n", pid);
  if (WIFEXITED(status)) {
    printf("proc-clone3: child exited with code %d\n", WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    printf("proc-clone3: child killed by signal %d\n", WTERMSIG(status));
  }
  printf("proc-clone3: elapsed %.6f s\n", end - start);

  return 0;
}

#else  // not __linux__

#include <stdio.h>

int main(void) {
  fprintf(stderr, "proc-clone3: this program is only supported on Linux.\n");
  return 1;
}

#endif
