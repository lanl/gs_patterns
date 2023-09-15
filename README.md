# Description
Memory analysis tool for finding gather / scatter (gs) accesses from DynamoRio traces. gs_patterns discovers gather/scatters from analyzing access patterns in memory traces (doesn't just look for gs instructions). gs_patterns writes the "subtraces" to a binary trace and spatter yaml format. The source lines of the top aggressors are reported. Use the provided pin clients in the pin_tracing folder or use DynamoRio. Pin tends to be more reliable for larger applications.

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
Binary file should be compiled with symbols turned on (-g)

# How gs_patterns works
g/s accesses are found by looking at repeated instruction addresses (loops) that are memory instructions (scalar and vector). The first pass finds the top g/s's. The second pass focuses on the top g/s accesses and records the normalized address distances to a binary file and spatter yaml.

# License
BSD-3 License. See [the LICENSE file](https://github.com/lanl/gs_patterns/blob/main/LICENSE).
  
# Author
Kevin Sheridan, <kss@lanl.gov>
