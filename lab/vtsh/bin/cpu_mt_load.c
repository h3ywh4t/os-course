#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef enum { ALGO_N2, ALGO_NLOGN } algo_t;

typedef struct {
  int thread_index;
  size_t array_size;
  unsigned long long repetitions;
  algo_t algo;
} worker_config_t;

static double now_seconds(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

static void bubble_sort(int* a, size_t n) {
  if (!a || n < 2) {
    return;
  }
  for (size_t i = 0; i < n; ++i) {
    int swapped = 0;
    for (size_t j = 1; j < n - i; ++j) {
      if (a[j - 1] > a[j]) {
        int tmp = a[j];
        a[j] = a[j - 1];
        a[j - 1] = tmp;
        swapped = 1;
      }
    }
    if (!swapped) {
      break;
    }
  }
}

static void merge(int* arr, int* tmp, size_t left, size_t mid, size_t right) {
  size_t i = left;
  size_t j = mid;
  size_t k = left;

  while (i < mid && j < right) {
    if (arr[i] <= arr[j]) {
      tmp[k++] = arr[i++];
    } else {
      tmp[k++] = arr[j++];
    }
  }
  while (i < mid) {
    tmp[k++] = arr[i++];
  }
  while (j < right) {
    tmp[k++] = arr[j++];
  }
  for (i = left; i < right; ++i) {
    arr[i] = tmp[i];
  }
}

static void merge_sort_rec(int* arr, int* tmp, size_t left, size_t right) {
  if (right - left <= 1) {
    return;
  }
  size_t mid = left + (right - left) / 2;
  merge_sort_rec(arr, tmp, left, mid);
  merge_sort_rec(arr, tmp, mid, right);
  merge(arr, tmp, left, mid, right);
}

static void merge_sort(int* arr, int* tmp, size_t n) {
  if (!arr || n < 2) {
    return;
  }
  merge_sort_rec(arr, tmp, 0, n);
}

static void* cpu_worker(void* arg) {
  worker_config_t* cfg = (worker_config_t*)arg;

  int* data = (int*)malloc(cfg->array_size * sizeof(int));
  if (!data) {
    fprintf(
        stderr,
        "cpu-mt-load: thread %d cannot allocate data array\n",
        cfg->thread_index
    );
    return NULL;
  }

  int* tmp = NULL;
  if (cfg->algo == ALGO_NLOGN) {
    tmp = (int*)malloc(cfg->array_size * sizeof(int));
    if (!tmp) {
      fprintf(
          stderr,
          "cpu-mt-load: thread %d cannot allocate tmp array\n",
          cfg->thread_index
      );
      free(data);
      return NULL;
    }
  }

  unsigned int seed = (unsigned int)time(NULL) ^
                      (unsigned int)(uintptr_t)pthread_self() ^
                      (unsigned int)cfg->thread_index;

  double t0 = now_seconds();

  for (unsigned long long rep = 0; rep < cfg->repetitions; ++rep) {
    for (size_t i = 0; i < cfg->array_size; ++i) {
      data[i] = (int)(rand_r(&seed) & 0x7fffffff);
    }

    if (cfg->algo == ALGO_N2) {
      bubble_sort(data, cfg->array_size);
    } else {
      merge_sort(data, tmp, cfg->array_size);
    }
  }

  double t1 = now_seconds();
  double elapsed = t1 - t0;

  fprintf(
      stderr,
      "cpu-mt-load: thread %d finished: size=%zu, reps=%llu, algo=%s, "
      "elapsed=%.6f s\n",
      cfg->thread_index,
      cfg->array_size,
      (unsigned long long)cfg->repetitions,
      (cfg->algo == ALGO_N2) ? "n2" : "nlogn",
      elapsed
  );

  free(tmp);
  free(data);
  return NULL;
}

static void print_usage(const char* prog) {
  fprintf(
      stderr,
      "Usage: %s <threads> <algo> <array_size> <repetitions_per_thread>\n"
      "\n"
      "  threads               - number of worker threads (>0)\n"
      "  algo                  - n2 or nlogn\n"
      "  array_size            - number of elements in array (>0)\n"
      "  repetitions_per_thread- how many times each thread sorts array\n"
      "\n"
      "Example:\n"
      "  %s 4 n2 200000 200\n"
      "  %s 8 nlogn 1000000 100\n",
      prog,
      prog,
      prog
  );
}

int main(int argc, char** argv) {
  if (argc != 5) {
    print_usage(argv[0]);
    return 1;
  }

  char* endptr = NULL;
  errno = 0;
  long threads_long = strtol(argv[1], &endptr, 10);
  if (errno != 0 || endptr == argv[1] || *endptr != '\0' || threads_long <= 0) {
    fprintf(stderr, "cpu-mt-load: invalid threads value '%s'\n", argv[1]);
    print_usage(argv[0]);
    return 1;
  }
  int threads = (int)threads_long;

  algo_t algo;
  if (strcmp(argv[2], "n2") == 0) {
    algo = ALGO_N2;
  } else if (strcmp(argv[2], "nlogn") == 0) {
    algo = ALGO_NLOGN;
  } else {
    fprintf(
        stderr,
        "cpu-mt-load: invalid algo '%s' (expected n2 or nlogn)\n",
        argv[2]
    );
    print_usage(argv[0]);
    return 1;
  }

  errno = 0;
  unsigned long array_size_ul = strtoul(argv[3], &endptr, 10);
  if (errno != 0 || endptr == argv[3] || *endptr != '\0' ||
      array_size_ul == 0) {
    fprintf(stderr, "cpu-mt-load: invalid array_size '%s'\n", argv[3]);
    print_usage(argv[0]);
    return 1;
  }
  size_t array_size = (size_t)array_size_ul;

  errno = 0;
  unsigned long long reps_ull = strtoull(argv[4], &endptr, 10);
  if (errno != 0 || endptr == argv[4] || *endptr != '\0' || reps_ull == 0) {
    fprintf(
        stderr, "cpu-mt-load: invalid repetitions_per_thread '%s'\n", argv[4]
    );
    print_usage(argv[0]);
    return 1;
  }
  unsigned long long repetitions = reps_ull;

  fprintf(
      stderr,
      "cpu-mt-load: starting with %d threads, algo=%s, "
      "array_size=%zu, reps_per_thread=%llu\n",
      threads,
      (algo == ALGO_N2) ? "n2" : "nlogn",
      array_size,
      (unsigned long long)repetitions
  );

  pthread_t* tids = (pthread_t*)calloc((size_t)threads, sizeof(pthread_t));
  worker_config_t* cfgs =
      (worker_config_t*)calloc((size_t)threads, sizeof(worker_config_t));

  if (!tids || !cfgs) {
    fprintf(stderr, "cpu-mt-load: cannot allocate threads/cfgs arrays\n");
    free(tids);
    free(cfgs);
    return 1;
  }

  double t0 = now_seconds();

  for (int i = 0; i < threads; ++i) {
    cfgs[i].thread_index = i;
    cfgs[i].array_size = array_size;
    cfgs[i].repetitions = repetitions;
    cfgs[i].algo = algo;

    int rc = pthread_create(&tids[i], NULL, cpu_worker, &cfgs[i]);
    if (rc != 0) {
      fprintf(
          stderr,
          "cpu-mt-load: pthread_create failed for thread %d (rc=%d)\n",
          i,
          rc
      );
      threads = i;
      break;
    }
  }

  int created_threads = threads;
  for (int i = 0; i < created_threads; ++i) {
    (void)pthread_join(tids[i], NULL);
  }

  double t1 = now_seconds();
  double elapsed = t1 - t0;

  unsigned long long total_sorts =
      (unsigned long long)created_threads * repetitions;
  unsigned long long total_elements =
      total_sorts * (unsigned long long)array_size;

  printf("cpu-mt-load summary:\n");
  printf("  threads:          %d\n", created_threads);
  printf("  algo:             %s\n", (algo == ALGO_N2) ? "n2" : "nlogn");
  printf("  array_size:       %zu\n", array_size);
  printf("  reps_per_thread:  %llu\n", (unsigned long long)repetitions);
  printf("  total_sorts:      %llu\n", total_sorts);
  printf("  total_elements:   %llu\n", total_elements);
  printf("  elapsed:          %.6f s\n", elapsed);
  if (elapsed > 0.0) {
    double sorts_per_sec = (double)total_sorts / elapsed;
    double elems_per_sec = (double)total_elements / elapsed;
    printf("  sorts/sec:        %.2f\n", sorts_per_sec);
    printf("  elements/sec:     %.2f\n", elems_per_sec);
  }

  free(tids);
  free(cfgs);
  return 0;
}