# Description

Memory analysis tool for finding scalar and vector gather / scatter (g/s) accesses from DynamoRio traces. gs_patterns writes the "subtraces" to a binary trace and spatter yaml format. The source lines of the top aggressors are reported. 

# Use
```
gs_pattern <trace.gz> <binary>
```
Binary file should be compiled with symbols turned on (-g)

# How gs_patterns works
g/s accesses are found by looking at repeated instruction addresses (loops) that are memory instructions (scalar and vector). The first pass finds the top g/s instructions. The second pass focuses on the top g/s accesses and records the normalized address distances to a binary file and spatter yaml.

    
  

