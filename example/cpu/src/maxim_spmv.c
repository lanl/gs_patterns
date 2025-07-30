#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <math.h>
#include <string.h>
#include "mmio.h"

typedef struct {
	int i;
	int j;
	double v;
} elem;

int compare(const void *a, const void *b) {
	elem lhs = *(elem *) a;
	elem rhs = *(elem *) b;
	return (lhs.i == rhs.i) ? (lhs.j - rhs.j) : (lhs.i - rhs.i);
}

int read(const char *fname, int *M, int *N, int **row, int **col, double **val) {
	int ret_code;
    MM_typecode matcode;
    FILE *f;
    int nz;

    if ((f = fopen(fname, "r")) == NULL) {
		fprintf(stderr, "error in fopen\n");
		exit(1);
    }
    if (mm_read_banner(f, &matcode) != 0) {
        fprintf(stderr, "error reading matrix banner\n");
        exit(1);
    }
    if (!mm_is_real(matcode) || !mm_is_matrix(matcode) || !mm_is_coordinate(matcode)) {
        fprintf(stderr, "unsupported ");
        fprintf(stderr, "Market Market type: [%s]\n", mm_typecode_to_str(matcode));
        exit(1);
    }
    if ((ret_code = mm_read_mtx_crd_size(f, M, N, &nz)) !=0) {
		fprintf(stderr, "error reading matrix size\n");
        exit(1);
	}

    // reserve memory for coordinate form
	elem *coords = (elem *) malloc (nz * sizeof(elem));

	// read
	for (int i = 0; i < nz; i++) {
		if (fscanf(f, "%d %d %lg\n", &coords[i].i, &coords[i].j, &coords[i].v) != 3) {
			fprintf(stderr, "invalid format at entry %d\n", i+1);
		}
	}

	// sort
	qsort(coords, nz, sizeof(elem), compare);

	// reserve memory for CSR
	*row = (int *) malloc ((*M+1) * sizeof(int));
	*col = (int *) malloc (nz * sizeof(int));
	*val = (double *) malloc (nz * sizeof(double));
	for (int i = 0; i < (*M)+1; i++) { (*row)[i] = 0; }

	// convert to CSR
	for (int i = 0; i < nz; i++) {
		(*row)[coords[i].i]++;
		(*col)[i] = coords[i].j - 1;
		(*val)[i] = coords[i].v;
	}
	for (int i = 0; i < *M; i++) {
		(*row)[i+1] += (*row)[i];
	}

	fclose(f);
	free(coords);
	return nz;
}

void set_sparse_vector(int N, double *vec) {

	int* scatter = (int *) malloc (N * sizeof(int));

	for (int i = 0; i < N; i++) {
		scatter[i] = rand() % N;
	}
	
	for (int i = 0; i < N; i++) {
		vec[scatter[i]] = i;
	}

	free(scatter);
}

void spmv_serial(int M, int *row, int *col, double *val, double *vec, double *res) {
	for (int i = 0; i < M; i++) {
		res[i] = 0;
		for (int j = row[i]; j < row[i+1]; j++) {
			res[i] += val[j] * vec[col[j]];
		}
	}
	return;
}

void spmv_omp(int M, int *row, int *col, double *val, double *vec, double *res, int num_threads) {
	#pragma omp parallel for num_threads(num_threads)
	for (int i = 0; i < M; i++) {
		res[i] = 0;
		for (int j = row[i]; j < row[i+1]; j++) {
			res[i] += val[j] * vec[col[j]];
		}
	}
}

int compare_results(double *a, double *b, int size, double tol) {
	for (int i = 0; i < size; i++) {
		if (fabs(a[i] - b[i]) > tol) {
			return 0; // match
		}
	}
	return 1; // mismatch
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s -f [martix-market-filename] -t [num_thread]\n", argv[0]);
		exit(1);
	}
	
	int num_threads = 1;
	int f_idx = 0;
	int print = 0;
	
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0) {
			f_idx = i + 1;
		} else if (strcmp(argv[i], "-t") == 0) {
			printf("num_thread: %s\n", argv[i+1]);
			num_threads = atoi(argv[i+1]);
		}
	}

	int M, N, *row, *col;
	double *val, *vec, *res;
	
	read(argv[f_idx], &M, &N, &row, &col, &val);

	vec = (double *) malloc (N * sizeof(double));
	res = (double *) malloc (M * sizeof(double));

	// For the evaluation of vectorized g/s instruction
	set_sparse_vector(N, vec);
	
	if (num_threads <= 1) {
		spmv_serial(M, row, col, val, vec, res);
	} else {
		spmv_omp(M, row, col, val, vec, res, num_threads);
	}

	free(row);
	free(col);
	free(val);
	free(vec);
	free(res);
	return 0;
}
