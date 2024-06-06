# Description
Memory analysis tool for finding nontrivial gather / scatter (g/s) accesses from DynamoRio formatted traces. gs_patterns doesn't just look for explicit g/s instructions, but also all other scalar accesses in loops. gs_patterns writes the subtraces to binary traces and a spatter yaml formatted file. The source lines of the top aggressors are reported. Use the provided pin clients in the pin_tracing folder or use DynamoRio. Pin tends to be more reliable for larger applications.

# Build
```
mkdir build
cd build
cmake ..
make
```

# Use
```
gs_pattern <trace.gz> <binary>
```
trace file should be gzipped (not tar + gz). Binary file should be compiled with symbols turned on (-g)

# How gs_patterns works
g/s accesses are found by looking at repeated instruction addresses (loops) that are memory instructions (scalar and vector). The first pass finds the top g/s's and filters out instructions with trivial access patterns. The second pass focuses on the top g/s accesses and records the normalized address array indices to a binary file and spatter yaml file.

# License
BSD-3 License. See [the LICENSE file](https://github.com/lanl/gs_patterns/blob/main/LICENSE).
  
# Author
Kevin Sheridan, <kss@lanl.gov>
