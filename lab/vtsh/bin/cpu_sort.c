#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void bubble_sort(int* data, size_t n) {
  if (n < 2) {
    return;
  }
  for (size_t i = 0; i < n - 1; ++i) {
    for (size_t j = 0; j < n - 1 - i; ++j) {
      if (data[j] > data[j + 1]) {
        int tmp = data[j];
        data[j] = data[j + 1];
        data[j + 1] = tmp;
      }
    }
  }
}

static void merge_sort_recursive(
    int* data, int* tmp, size_t left, size_t right
) {
  if (right - left <= 1) {
    return;
  }
  size_t mid = left + (right - left) / 2;
  merge_sort_recursive(data, tmp, left, mid);
  merge_sort_recursive(data, tmp, mid, right);

  size_t i = left;
  size_t j = mid;
  size_t k = left;

  while (i < mid && j < right) {
    if (data[i] <= data[j]) {
      tmp[k++] = data[i++];
    } else {
      tmp[k++] = data[j++];
    }
  }
  while (i < mid) {
    tmp[k++] = data[i++];
  }
  while (j < right) {
    tmp[k++] = data[j++];
  }
  for (i = left; i < right; ++i) {
    data[i] = tmp[i];
  }
}

static void merge_sort(int* data, size_t n) {
  if (n < 2) {
    return;
  }
  int* tmp = (int*)malloc(n * sizeof(int));
  if (tmp == NULL) {
    fprintf(stderr, "cpu-sort: failed to allocate temp buffer\n");
    exit(1);
  }
  merge_sort_recursive(data, tmp, 0, n);
  free(tmp);
}

static void fill_random(int* data, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    data[i] = rand();
  }
}

static void usage(const char* prog) {
  fprintf(
      stderr,
      "Usage: %s <algo> <array_size> <repetitions>\n"
      "  <algo>         nlogn | n2\n"
      "  <array_size>   positive integer\n"
      "  <repetitions>  positive integer (how many times to repeat sort)\n",
      prog
  );
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    usage(argv[0]);
    return 1;
  }

  const char* algo = argv[1];

  char* endptr = NULL;
  errno = 0;
  unsigned long long n_val = strtoull(argv[2], &endptr, 10);
  if (errno != 0 || *endptr != '\0' || n_val == 0) {
    fprintf(stderr, "cpu-sort: invalid array_size '%s'\n", argv[2]);
    return 1;
  }

  errno = 0;
  unsigned long long reps_val = strtoull(argv[3], &endptr, 10);
  if (errno != 0 || *endptr != '\0' || reps_val == 0) {
    fprintf(stderr, "cpu-sort: invalid repetitions '%s'\n", argv[3]);
    return 1;
  }

  size_t n = (size_t)n_val;
  size_t reps = (size_t)reps_val;

  int* data = (int*)malloc(n * sizeof(int));
  if (data == NULL) {
    fprintf(stderr, "cpu-sort: failed to allocate %zu ints\n", n);
    return 1;
  }

  srand((unsigned int)time(NULL));

  clock_t start = clock();

  if (strcmp(algo, "nlogn") == 0) {
    for (size_t r = 0; r < reps; ++r) {
      fill_random(data, n);
      merge_sort(data, n);
    }
  } else if (strcmp(algo, "n2") == 0) {
    for (size_t r = 0; r < reps; ++r) {
      fill_random(data, n);
      bubble_sort(data, n);
    }
  } else {
    fprintf(stderr, "cpu-sort: unknown algo '%s'\n", algo);
    free(data);
    return 1;
  }

  clock_t end = clock();
  double elapsed = 0.0;
  if (end >= start) {
    elapsed = (double)(end - start) / (double)CLOCKS_PER_SEC;
  }

  printf(
      "cpu-sort: algo=%s, size=%zu, reps=%zu, elapsed=%.3f s\n",
      algo,
      n,
      reps,
      elapsed
  );

  free(data);
  return 0;
}
