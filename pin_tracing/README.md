
Download PIN tool here:

https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html

$ module load gcc cmake #or make sure you have gcc and cmake

$ tar zxvf <pin.file.tar.gz>
$ export PIN_DIR=<pin_dir>
$ cp -rv ImemROI $PIN_DIR/source/tools
$ cd $PIN_DIR/source/tools

#add ImemROI to the list of clients
$ vim makefile

#with gcc
$ make -j

There are three clients. In theory you can run your application with threads and processes (ranks), but only process 0 with thread 0 will be used.

ImemInscount: 
Outputs instruction count to stdout and to file inscount.out

ImemROIThreads:
Outputs dynamorio trace file(s) of each function trace.
Requires a file called roi_funcs.txt to have line by line names of functions you want to trace. Case is ignored.
Example
main
calc_pi

ImemROIThreadsRange:
Outputs dynamorzio trace file of instruction range provided.
Requires a file called roi_range.txt with beginning instruction, end instruction and total instructions
Example
2000
20000
500000

For ImemROIThreadsRange, I first like to use ImemInscount to get the total number of instructions. Also, as a convenience, ImemROIThreads prints out the instruction percentage of the run a long with the applications stdout/stderr. That way you can hone in on the range you want.

Example:
$PIN_DIR/pin -t $PIN_DIR/source/tools/ImemROI/obj-intel64/ImemInscount.so -- ./hello
$PIN_DIR/pin -t $PIN_DIR/source/tools/ImemROI/obj-intel64/ImemROIThreadsRange.so -- ./hello
$PIN_DIR/pin -t $PIN_DIR/source/tools/ImemROI/obj-intel64/ImemROITHREADS.so -- ./hello

