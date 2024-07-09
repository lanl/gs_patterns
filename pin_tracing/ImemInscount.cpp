
#include <string>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <dirent.h>
#include "pin.H"

#define MAXBYTES (1LL<<37) //32GiB
#define MAXTHREADS (256)

//threads
bool isActive[MAXTHREADS] = {false};

//pe
bool doTrace = true;
bool stopTrace = false;
bool isROI = true;
INT32 pid;
INT32 nfuncs = 0;
INT64 totalbytes = 0;
UINT64 ROI_A = 0;
UINT64 ROI_B = 0;
UINT64 Icnt = 0;
UINT64 Mcnt = 0;
UINT64 gIcnt = 0;
UINT64 gMcnt = 0;

#define PADSIZE 56 // 64 byte line size: 64-8
#define NBUFS (1024)
INT32 numThreads = 0;

#define PADSIZE 56 // 64 byte line size: 64-8
#define NBUFS (1024)

//FROM DR SOURCE
//DR trace
typedef uintptr_t addr_t;

struct _trace_entry_t {
  unsigned short type; 
  unsigned short size;
  addr_t addr; 
}  __attribute__((packed));
typedef struct _trace_entry_t trace_entry_t;

FILE * fptrace;
trace_entry_t * ptrace = NULL; 
trace_entry_t btrace[NBUFS+1];

// a running count of the instructions
class thread_data_t {
  public:
    thread_data_t() : _count(0) {}
    UINT64 _count;
    UINT8 _pad[PADSIZE];
};

// key for accessing TLS storage in the threads. initialized once in main()
static TLS_KEY tls_key = INVALID_TLS_KEY;

INT32 read_range_file(const char * roi_file) {

  FILE * roi_p = NULL;
  
  roi_p = fopen(roi_file, "r");
  if (roi_p == NULL) {
    printf("PIN -- ERROR: Could not open %s!\n", roi_file);
    return -1;
  }

  fscanf(roi_p, "%lu %lu\n", &ROI_A, &ROI_B);
  
  fclose(roi_p);

  return 0;
  
}

//string tools
int startswith(const char *a, const char *b) {
  if(strncmp(b, a, strlen(b)) == 0)
    return 1;
  return 0;
}

//set active thread 0 on PE 0 only, ignore rest
VOID set_active_first_pe_thread() {
  
  INT32 aPid;
  CHAR pid_name[1024];
  THREADID threadid = PIN_ThreadId();
  FILE * fppid;
  struct dirent *de;  // Pointer for directory entry
  DIR *dr = NULL;  

  //Only thread ONE is active
  if (threadid != 0) {
    sleep(16);
    return;
    
  } else {
    isActive[threadid] = true;
  }

  //check current dir for existing tag files
  dr = opendir(".");  
  if (dr == NULL) {
    printf("PIN -- Could not open current directory" );
    PIN_ExitProcess(1);
  }
  
  while ((de = readdir(dr)) != NULL) {
    
    if ( startswith(de->d_name, "tag.")) {
      printf("PIN -- ERROR tag files exist. Remove all tag.*.pid files and rerun.\n");
      PIN_ExitProcess(1);
    }
  }  
  closedir(dr);
  
  // Open tag file to determine pid
  pid = PIN_GetPid();
  sprintf(pid_name, "tag.%d.pid", pid);
  fppid = fopen(pid_name, "w");
  if (fppid == NULL) {
    printf("PIN -- ERROR: Could not open %s for writing!\n", pid_name);
    PIN_ExitProcess(1);
  }
  fclose(fppid);
  sleep(15);
  
  dr = opendir(".");  
  if (dr == NULL) {
    printf("PIN -- Could not open current directory" );
    PIN_ExitProcess(1);
  }
  
  while ((de = readdir(dr)) != NULL) {
    
    if ( startswith(de->d_name, "tag.")) {

      sscanf(de->d_name, "tag.%d.pid", &aPid);
      if (pid > aPid) {
	doTrace = false;
	break;
      }
    }
  }  
  closedir(dr);
  
  sleep(15);

  remove(pid_name);

  return;

}


VOID PeStart(VOID* v) {  

  THREADID threadid = PIN_ThreadId();
  set_active_first_pe_thread();

  if (!isActive[threadid])
    return;

  if (!doTrace)
    return;

  /*
  //Open new trace file
  pid = PIN_GetPid();
  sprintf(trace_name, "roitrace.%d.bin", pid);
  fptrace = fopen(trace_name, "w");
  if (fptrace == NULL) {
    printf("PIN -- ERROR: Could not open %s for writing!\n", trace_name);
    PIN_ExitProcess(1);
  }
  fclose(fptrace);
  */
    
}

VOID ThreadStart(THREADID threadid, CONTEXT* ctxt, INT32 flags, VOID* v) {

  if (threadid == 0)
    isActive[threadid] = true;
  
  numThreads++;
  thread_data_t* tdata = new thread_data_t;
  if (PIN_SetThreadData(tls_key, tdata, threadid) == FALSE) {
    printf("ERROR: PIN_SetThreadData failed\n");
    PIN_ExitProcess(1);
  }

  //Only thread 0
  if (!isActive[threadid])
    return;

  //Only PE 0
  if (!doTrace) {
    return;
  }
            
}

// This function is called when the thread exits
VOID ThreadFini(THREADID threadIndex, const CONTEXT* ctxt, INT32 code, VOID* v) {
  
  
  thread_data_t* tdata = static_cast< thread_data_t* >(PIN_GetThreadData(tls_key, threadIndex));
  //printf("MCount[%u] = %lu\n", threadIndex, Mcnt[threadIndex]);
  //printf("ICount[%u] = %lu\n", threadIndex, Icnt[threadIndex]);
  delete tdata;
    
  THREADID threadid= PIN_ThreadId();
  if (!isActive[threadid])
    return;
  
  if (!doTrace)
    return;

  FILE * fp;
  fp = fopen("inscount.out", "w");
  fprintf(fp, "Count %lu\n", gIcnt);
  fclose(fp);
  //printf("PIN -- Instrs = %lu -  %lu\n", ROI_A, ROI_B);
  printf("PIN --   ROI Instrs      %lu\n", Icnt);
  printf("PIN --   ROI MemInstrs   %lu\n", Mcnt);
  printf("PIN --   File            inscount.out\n");  
  printf("PIN -- \n");

}

// Print a memory read record
//VOID RecordMemRead(VOID * ip, VOID * addr, USIZE bsize, CHAR * rtn) {
VOID RecordMemRead(VOID * ip, VOID * addr, USIZE bsize, THREADID threadid) {
      
  if (!doTrace)
    return;
  
  if (!isActive[threadid])
    return;
  
  if (stopTrace)
    return;
  
  if(!isROI)
    return;
    
  Mcnt++;
}

// Print a memory write record
//VOID RecordMemWrite(VOID * ip, VOID * addr, USIZE bsize, CHAR * rtn) {
VOID RecordMemWrite(VOID * ip, VOID * addr, USIZE bsize, THREADID threadid) {
    
  if (!doTrace)
    return;
    
  if (!isActive[threadid])
    return;
  
  if (stopTrace)
    return;  
  
  if (!isROI)
    return;
    
  Mcnt++;
}

//VOID RecordInstr(VOID * ip, USIZE bsize, VOID * rtn) {
VOID RecordInstr(VOID * ip, USIZE bsize, THREADID threadid) {
    
      
  if (!isActive[threadid])
    return;
  
  gIcnt++;
      
  if (!doTrace)
    return;  
  
  if (stopTrace)
    return;
  
  if(!isROI)
    return;
  
  Icnt++;

}

VOID RecordMemScattered(IMULTI_ELEMENT_OPERAND* memOpInfo, THREADID threadid) {
  
  if (!doTrace)
    return;
  
  if (!isActive[threadid])
    return;
      
  if (stopTrace)
    return;
  
  if(!isROI)
    return;
            
  for (UINT32 j = 0; j < memOpInfo->NumOfElements(); j++) {
    
    Mcnt++;
    
  }
}



// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v) {
  
  // Instruments memory accesses using a predicated call, i.e.
  // the instrumentation is called iff the instruction will actually be executed.
  //
  // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
  // prefixed instructions appear as predicated instructions in Pin.
    
  
  UINT32 memOperands = INS_MemoryOperandCount(ins);
  
  USIZE Isize;
  
  Isize = INS_Size (ins);
  
    // Get routine name if valid
  //const CHAR * name = "invalid";
  
  //if(RTN_Valid(INS_Rtn(ins))) {
  //  name = RTN_Name(INS_Rtn(ins)).c_str();
  //}
  
  INS_InsertPredicatedCall(
			   ins, IPOINT_BEFORE, (AFUNPTR)RecordInstr,
			   IARG_INST_PTR,
			   IARG_UINT32, Isize,
			   //IARG_PTR, name,
			   IARG_THREAD_ID,
			   IARG_END);
  
  if (INS_HasScatteredMemoryAccess(ins) ) {
    
    if (INS_IsValidForIarg(ins, IARG_MULTI_ELEMENT_OPERAND)) {
    
        for (UINT32 op=0; op < INS_OperandCount(ins); op++) {
	  
            if (INS_OperandIsMemory(ins, op) &&         // Skip register operands
                INS_OperandElementCount(ins, op) > 1) {  // Operand must have elements

	      INS_InsertCall( ins, IPOINT_BEFORE, (AFUNPTR)RecordMemScattered,
			      IARG_MULTI_ELEMENT_OPERAND, op,
			      IARG_THREAD_ID,
			      IARG_END);
            }
        }
    }    
    return;
  }
  
  //if (INS_HasScatteredMemoryAccess(ins) ) {
  //  return;
  //}
  // Iterate over each memory operand of the instruction.
  for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
    
    UINT32 Msize = INS_MemoryOperandSize(ins, memOp);
    
    if (INS_MemoryOperandIsWritten(ins, memOp)) {
      INS_InsertPredicatedCall(
			       ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
			       IARG_INST_PTR,
			       IARG_MEMORYOP_EA, memOp,
			       IARG_UINT32, Msize,
			       //IARG_ADDRINT, name,
			       IARG_THREAD_ID,
			       IARG_END);
    }
    
    if (INS_MemoryOperandIsRead(ins, memOp)) {
      INS_InsertPredicatedCall(
			       ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
			       IARG_INST_PTR,
			       IARG_MEMORYOP_EA, memOp,
			       IARG_UINT32, Msize,
			       //IARG_ADDRINT, name,
			       IARG_THREAD_ID,
			       IARG_END);
    }
    
    // Note that in some architectures a single memory operand can be 
    // both read and written (for instance incl (%eax) on IA-32)
    // In that case we instrument it once for read and once for write.
  }
}


// Pin calls this function at the end
VOID Fini(INT32 code, VOID *v) {
  //*OutFile << "Total number of threads = " << numThreads << endl;
  //fclose(trace);
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage() {
    PIN_ERROR( "This Pintool prints a trace of memory addresses\n" 
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}


/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[]) {
  
  // Initialize symbol table code, needed for rtn instrumentation
  PIN_InitSymbols();
  
  // Usage
  if (PIN_Init(argc, argv))
    return Usage();

  tls_key = PIN_CreateThreadDataKey(NULL);
  if (tls_key == INVALID_TLS_KEY) {
    printf("number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit\n");
    PIN_ExitProcess(1);
  }
  
  PeStart(NULL);
  
  // Register ThreadStart to be called when a thread starts.
  PIN_AddThreadStartFunction(ThreadStart, NULL);

  // Register Fini to be called when thread exits.
  PIN_AddThreadFiniFunction(ThreadFini, NULL);
  
  // Register Fini to be called when the application exits.
  PIN_AddFiniFunction(Fini, NULL);
    
  // Add instrument functions
  INS_AddInstrumentFunction(Instruction, 0);
  
  // Never returns
  PIN_StartProgram();
  
  return 0;
}
