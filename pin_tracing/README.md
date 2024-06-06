# Setup
Download PIN tool here:
```
#tested with version 3.28
https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html

#via
wget https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.28-98749-g6643ecee5-gcc-linux.tar.gz
```
```
module load gcc #or make sure you have gcc. Tested with 9.4.0 and 11.3.0

tar zxvf <pin.file.tar.gz>

export PIN_DIR=<pin_dir>  # full path

cp -rv pin_tracing/ImemROI $PIN_DIR/source/tools

cd $PIN_DIR/source/tools

#add ImemROI to the list of clients in makefile
vim makefile

#Compile clients. Some clients may not compile, that is OK.
make -j

#check that clients compiled:
ls -al $PIN_DIR/source/tools/ImemROI/obj-intel64/*.so
make -j
```

There are three clients. You can run your application with multiple threads and multiple processes (ranks), but only process 0 with thread 0 will be used. *** NOTE *** make sure you gzip the trace before using gs_patterns.

# ImemInscount 
Outputs instruction count to stdout and to inscount.out

# ImemROIThreads
Outputs dynamorio trace formatted file(s) of each function. Requires a file called roi_funcs.txt (in your run dir) with a line per function name you want to trace. Case is ignored.
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
gzip roitrace.bin
```
