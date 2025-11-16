#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  long id;
  char word[9];
} Row;

static Row* read_table(const char* path, size_t* out_size) {
  FILE* f = fopen(path, "r");
  if (f == NULL) {
    fprintf(
        stderr, "ema-join-sm: cannot open '%s': %s\n", path, strerror(errno)
    );
    return NULL;
  }

  size_t n = 0;
  if (fscanf(f, "%zu", &n) != 1) {
    fprintf(stderr, "ema-join-sm: bad header in '%s'\n", path);
    fclose(f);
    return NULL;
  }

  Row* rows = (Row*)malloc(n * sizeof(Row));
  if (rows == NULL) {
    fprintf(stderr, "ema-join-sm: failed to allocate %zu rows\n", n);
    fclose(f);
    return NULL;
  }

  for (size_t i = 0; i < n; ++i) {
    if (fscanf(f, "%ld %8s", &rows[i].id, rows[i].word) != 2) {
      fprintf(stderr, "ema-join-sm: bad row %zu in '%s'\n", i, path);
      free(rows);
      fclose(f);
      return NULL;
    }
    rows[i].word[8] = '\0';
  }

  fclose(f);
  *out_size = n;
  return rows;
}

static int cmp_row_by_id(const void* a, const void* b) {
  const Row* ra = (const Row*)a;
  const Row* rb = (const Row*)b;
  if (ra->id < rb->id) {
    return -1;
  }
  if (ra->id > rb->id) {
    return 1;
  }
  return 0;
}

static size_t count_join(const Row* a, size_t na, const Row* b, size_t nb) {
  size_t i = 0;
  size_t j = 0;
  size_t count = 0;

  while (i < na && j < nb) {
    if (a[i].id < b[j].id) {
      ++i;
    } else if (a[i].id > b[j].id) {
      ++j;
    } else {
      long key = a[i].id;

      size_t ia = i;
      while (i < na && a[i].id == key) {
        ++i;
      }
      size_t ja = j;
      while (j < nb && b[j].id == key) {
        ++j;
      }

      size_t left_count = i - ia;
      size_t right_count = j - ja;
      count += left_count * right_count;
    }
  }

  return count;
}

static void write_join(
    FILE* out, const Row* a, size_t na, const Row* b, size_t nb
) {
  size_t i = 0;
  size_t j = 0;

  while (i < na && j < nb) {
    if (a[i].id < b[j].id) {
      ++i;
    } else if (a[i].id > b[j].id) {
      ++j;
    } else {
      long key = a[i].id;

      size_t ia = i;
      while (i < na && a[i].id == key) {
        ++i;
      }
      size_t ja = j;
      while (j < nb && b[j].id == key) {
        ++j;
      }

      for (size_t ii = ia; ii < i; ++ii) {
        for (size_t jj = ja; jj < j; ++jj) {
          fprintf(out, "%ld %s %s\n", key, a[ii].word, b[jj].word);
        }
      }
    }
  }
}

static void usage(const char* prog) {
  fprintf(
      stderr,
      "Usage: %s <left_table> <right_table> <output>\n"
      "  Each input file format:\n"
      "    first line: N (number of rows)\n"
      "    next N lines: <id> <8-char-word>\n",
      prog
  );
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    usage(argv[0]);
    return 1;
  }

  size_t left_size = 0;
  size_t right_size = 0;

  Row* left = read_table(argv[1], &left_size);
  if (left == NULL) {
    return 1;
  }
  Row* right = read_table(argv[2], &right_size);
  if (right == NULL) {
    free(left);
    return 1;
  }

  qsort(left, left_size, sizeof(Row), cmp_row_by_id);
  qsort(right, right_size, sizeof(Row), cmp_row_by_id);

  size_t join_rows = count_join(left, left_size, right, right_size);

  FILE* out = fopen(argv[3], "w");
  if (out == NULL) {
    fprintf(
        stderr,
        "ema-join-sm: cannot open output '%s': %s\n",
        argv[3],
        strerror(errno)
    );
    free(left);
    free(right);
    return 1;
  }

  fprintf(out, "%zu\n", join_rows);
  write_join(out, left, left_size, right, right_size);

  fclose(out);
  free(left);
  free(right);

  return 0;
}
