# Setup
Download PIN tool here:
```
https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html
```
```
module load gcc cmake #or make sure you have gcc and cmake

tar zxvf <pin.file.tar.gz>

export PIN_DIR=<pin_dir>

cp -rv pin_tracing/ImemROI $PIN_DIR/source/tools

cd $PIN_DIR/source/tools

#add ImemROI to the list of clients
vim makefile

#with gcc
make -j
```

There are three clients. In theory you can run your application with threads and processes (ranks), but only process 0 with thread 0 will be used.

# ImemInscount 
Outputs instruction count to stdout and to inscount.out

# ImemROIThreads
Outputs dynamorio trace file(s) of each function trace. Requires a file called roi_funcs.txt (in your run dir) with a line per function name you want to trace. Case is ignored.
Example
```
main
calc_pi
```

# ImemROIThreadsRange
Outputs dynamorio trace file of instruction range provided. Requires a file called roi_range.txt (in your run dir) with beginning instruction, end instruction and total instructions of entire application run (used for percentage complete). 
Example
```
2000
20000
500000
```
# Usage
For ImemROIThreadsRange, I first like to use ImemInscount to get the total number of instructions. Also, as a convenience, ImemROIThreadsRange prints out the instruction percentage progress along with the applications stdout/stderr. That way you can hone in on the range you want.

Example:
```
$PIN_DIR/pin -t $PIN_DIR/source/tools/ImemROI/obj-intel64/ImemInscount.so -- ./hello
$PIN_DIR/pin -t $PIN_DIR/source/tools/ImemROI/obj-intel64/ImemROIThreadsRange.so -- ./hello
$PIN_DIR/pin -t $PIN_DIR/source/tools/ImemROI/obj-intel64/ImemROIThreads.so -- ./hello
```
