#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <omp.h>

#include "../../misc/hooks/zsim_hooks.h"

void copy(unsigned n, double *a, double *b) {
#if defined(OMP_OFFLOAD)
#pragma omp target
  {
#endif
    zsim_PIM_function_begin();
#pragma omp parallel for
    for (unsigned i = 0; i < n; i++)
      a[i] = b[i];
    zsim_PIM_function_end();
#if defined(OMP_OFFLOAD)
  }
#endif
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <num_threads> <problem_size>\n", argv[0]);
    exit(1);
  }

  // Set number of threads
  unsigned num_threads = atoi(argv[1]);
  omp_set_num_threads(num_threads);

  // Init random number generation
  srand((unsigned)time(NULL));

  // Allocate the vectors
  unsigned n = atoi(argv[2]);
  double *vec_a = (double *)calloc(2 * n, sizeof(double)), *vec_b = vec_a + n;
  for (unsigned i = 0; i < n; i++) {
    vec_a[i] = (double)(3 * rand() + 0.5) / RAND_MAX;
    vec_b[i] = (double)(3 * rand() + 0.5) / RAND_MAX;
  }

  zsim_roi_begin();

#if defined(OMP_OFFLOAD)
  if (num_threads > 1) {
    fprintf(stderr,
            "OpenMP offload version shold be run with a single thread\n");
    exit(1);
  }
  omp_set_default_device(0);
#pragma omp target enter data map(to : vec_a [0:n], vec_b [0:n])
#endif

  // Call copy kernel
  copy(n, vec_a, vec_b);

#if defined(OMP_OFFLOAD)
#pragma omp target exit data map(delete : vec_a [0:n], vec_b [0:n])
#endif

  zsim_roi_end();

  free(vec_a);

  return 0;
}
