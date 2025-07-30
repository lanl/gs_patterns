# Description
Simple SpMV (Spare Matrix Vector) based GEMV kernel for cpu-pin tool example. Each ROI has either sparse gather or scatter instructions, or vectorization target for vector instruction. 

# Matrix Input
_.mtx_ file is used as an input from https://sparse.tamu.edu/HB/. Each _.mtx_ has a banner on the top in the form of _MatrixMarket banner1 banner2 banner3 ..._ For this example _matrix, coordinate, real_ are required and only compatible.

# ROI
 - spmv_serial: serial gemv kernel (activated w/o -t)
 - spmv_omp: omp gemv kernel (activated w/ -t <greater than 1>)
 - set_sparse_vector: scatter based sparse operand vector generator

# Use
## Compile & Run
```
gcc -g -fopenmp -o maxim_spmv src/*.c
./maxim_spmv -t <thread_num> -f mat/<mat_file>
```
## Run PIN
```
echo <ROI> > roi_funcs.txt
$PIN_ROOT/pin -t ../pin_tracing/obj-intel64/ImemROIThreads.so -- ./maxim_spmv -t <thread_num> -f mat/<mat_file>
```
## Run GS Pattern
```
gzip roitrace.00.<ROI>.bin
<gs_pattern_build_root>/gs_pattern roitrace.00.<ROI>.bin.gz maxim_spmv
```

# Troubleshoot
## PIN
If no ROI instructions are tracked, try with less or no compiler optimization options. gcc tends to be more reliable for the optimizations for this example.

## gs_pattern
If empty outputs are generated with the valid trace file, the fastest solutions you may consider would be to try with 1) _SYMBOLS_ONLY_ turned off or 2) larger problem size. From experience, lower threshold of the number of indices is ~100 for this example.  