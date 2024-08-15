# Description
Memory analysis tool for finding gather / scatter (gs) accesses from DynamoRio & NVBit traces. 
gs_patterns discovers gather/scatters from analyzing access patterns in memory traces (doesn't just look for gs instructions). gs_patterns writes the "subtraces" to a binary trace and spatter yaml format. 
The source lines of the top aggressors are reported.  

For CPU applications use the provided pin client in pin_tracing folder (or DynamoRio).  Pin tends to be more reliable for larger applications.

For CUDA kernels use the provided nvbit client in the nvbit_tracing folder. 

See the README in the respective folders for more detailed information on these tools.


# Build
```
mkdir build
cd build
cmake ..
make
```

# Use

## For Pin/DynamoRio
```
gs_pattern <pin_trace.gz> <binary>
```

## For NVBit (CUDA Kernels) 

```
gs_pattern <nvbit_trace.gz> -nv 
```

Trace file should be gzipped. For Pin or DynamoRio, Binary file should be compiled with symbols turned on (-g).

For NVBit tracing the kernel must be compiled with line numbers (--generate-line-info).  Please see nvbit_tracing/README.md for detailed information on how to extract traces for CUDA kernels which are compatible with gs_patterns.

# How gs_patterns works
g/s accesses are found by looking at repeated instruction addresses (loops) that are memory instructions (scalar and vector). 
The first pass finds the top g/s's. The second pass focuses on the top g/s accesses and records the normalized address distances to a binary file and spatter yaml.

# License
BSD-3 License. See [the LICENSE file](https://github.com/lanl/gs_patterns/blob/main/LICENSE).
  
# Author
Kevin Sheridan, <kss@lanl.gov>
