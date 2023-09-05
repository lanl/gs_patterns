
#include <string>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <dirent.h>
#include "pin.H"

#define MAXTHREADS (256)

//threads
bool isActive[MAXTHREADS] = {false};

//pe
bool doTrace = true;
bool stopTrace = false;
bool isROI = false;
bool do_print = true;
INT32 pid;
INT32 nfuncs = 0;
INT64 totalbytes = 0;
UINT64 ROI_A = 0;
UINT64 ROI_B = 0;
UINT64 ITOTAL = 0;
UINT64 IINTER = 0;
UINT64 Icnt = 0;
UINT64 Mcnt = 0;
UINT64 gIcnt = 0;
UINT64 gMcnt = 0;
UINT64 Scatteredcnt = 0;

INT64 maxbytes (1LL<<37); //128GiB

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

INT32 read_maxGB_file(const char * maxGB_file) {

  UINT64 maxGB;
  FILE * maxGB_p = NULL;
  
  maxGB_p = fopen(maxGB_file, "r");
  if (maxGB_p == NULL) {
    printf("PIN -- ERROR: Could not open %s!\n", maxGB_file);
    return -1;
  }

  fscanf(maxGB_p, "%lu\n", &maxGB);

  maxbytes = maxGB * (1024 * 1024 * 1024);
  
  fclose(maxGB_p);

  return 0;
  
}

INT32 read_range_file(const char * roi_file) {

  FILE * roi_p = NULL;
  
  roi_p = fopen(roi_file, "r");
  if (roi_p == NULL) {
    printf("PIN -- ERROR: Could not open %s!\n", roi_file);
    return -1;
  }

  fscanf(roi_p, "%lu %lu %lu\n", &ROI_A, &ROI_B, &ITOTAL);

  IINTER = ITOTAL / 100;

  printf("PIN -- ROI_A:  %16lu\n", ROI_A);
  printf("PIN -- ROI_B:  %16lu\n", ROI_B);
  printf("PIN -- ITOTAL: %16lu\n", ITOTAL);
  printf("PIN -- IINTER: %16lu\n", IINTER);
  fflush(stdout);
  
  fclose(roi_p);

  return 0;
  
}

//string tools
int startswith(const char *a, const char *b) {
  if(strncmp(b, a, strlen(b)) == 0)
    return 1;
  return 0;
}

VOID PeStart(VOID* v) {  

  //printf("PIN -- PeStart ing\n");
  int ret;

  INT32 aPid;
  CHAR trace_name[1024];
  THREADID threadid = PIN_ThreadId();

  if (threadid != 0) {
    sleep(16);
    return;
    
  } else {
    isActive[threadid] = true;
  }
  
  // Open trace file and write header
  pid = PIN_GetPid();
  sprintf(trace_name, "roitrace.%d.bin", pid);
  fptrace = fopen(trace_name, "w");
  if (fptrace == NULL) {
    printf("PIN -- ERROR: Could not open %s for writing!\n", trace_name);
    PIN_ExitProcess(1);
  }
  fclose(fptrace);

  sleep(15);

  // Only trace with PE0
  struct dirent *de;  // Pointer for directory entry
  
  DIR *dr = opendir(".");
  
  if (dr == NULL) {
    printf("PIN -- Could not open current directory" );
    PIN_ExitProcess(1);
  }
  
  while ((de = readdir(dr)) != NULL) {
    
    if ( startswith(de->d_name, "roitrace.")) {

      sscanf(de->d_name, "roitrace.%d.bin", &aPid);
      if (pid > aPid) {
	doTrace = false;
	break;
      }
    }
  }  
  closedir(dr);
  
  sleep(15);
  
  if (!doTrace) {
    remove(trace_name);
    return;
  }
  remove(trace_name);  

  if (access("maxGB.txt", F_OK) == 0) {
    ret = read_maxGB_file("maxGB.txt");
    if (ret) {
      printf("PIN -- ERROR: read_maxGB_file returned %d\n", ret);
      PIN_ExitProcess(1);
    }
  }
  
  //Get ROI functions
  ret = read_range_file("roi_range.txt");
  if (ret) {
    printf("PIN -- ERROR: read_rio_file returned %d\n", ret);
    PIN_ExitProcess(1);
  }    

  sprintf(trace_name, "roitrace..bin");
  fptrace = fopen(trace_name, "w");
  if (fptrace == NULL) {
    printf("PIN -- ERROR: Could not open %s for writing!\n", trace_name);
    PIN_ExitProcess(1);
  }
  //printf("PIN -- intrs: %lu - %lu\n", ROI_A, ROI_B);
    
}


void drtrace_write(FILE * fp, trace_entry_t * val, trace_entry_t ** p_val, int purge) {

  int ret;
  int idx;
  
  idx = NBUFS - (&val[NBUFS] - *p_val);

  if ( (idx == NBUFS) || (purge) ) {
    
    ret = fwrite(val, sizeof(trace_entry_t), idx, fp);
    if (ret != idx) {
      printf("PIN -- ERROR: fwrite failed line %d\n", __LINE__);
      PIN_ExitProcess(1);
    }
    totalbytes += (ret *  sizeof(trace_entry_t));

    if ( (totalbytes >= maxbytes) && (purge == 0) ) {
      printf("PIN -- maxbytes reached!\n");
      printf("PIN: %5.2f%c\n", 100.0 * (double) (gIcnt-1) / (double) ITOTAL, '%');
      FILE * fp;
      fp = fopen("max_reach.txt", "w");
      fprintf(fp, "max_perc: %5.2f", 100.0 * (double) (gIcnt-1) / (double) ITOTAL);
      fclose(fp);
      fflush(stdout);      
      
      stopTrace = true;
    }
    *p_val = val;    
  }
  
  return;
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
    
  trace_entry_t tent;
  ptrace = &btrace[0];
  
  //HEADER
  tent.type = (unsigned short) 0x19;
  tent.size = (unsigned short) 0;
  tent.addr = (addr_t) 1;  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
  
  //THREAD
  tent.type = (unsigned short) 0x16;
  tent.size = (unsigned short) 4;
  tent.addr = (addr_t) pid;  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
  
  //PID
  tent.type = (unsigned short) 0x18;
  tent.size = (unsigned short) 4;
  tent.addr = (addr_t) pid;  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
  
  //MARKER TIMESTAMP
  tent.type = (unsigned short) 0x1c;
  tent.size = (unsigned short) 2;
  tent.addr = (addr_t) 0x002eff22e15562f3;  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
  
  //MARKER CPUID
  tent.type = (unsigned short) 0x1c;
  tent.size = (unsigned short) 3;
  tent.addr = (addr_t) 0;  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
        
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
  
  printf("PIN -- Instrs = %lu -  %lu\n", ROI_A, ROI_B);
  printf("PIN -- MCount = %lu\n", Mcnt);
  printf("PIN -- ICount = %lu\n", Icnt);
  printf("PIN -- Scattr = %lu\n", Scatteredcnt);
  printf("PIN -- gICount = %lu\n", gIcnt);
  
  trace_entry_t tent;
  
  //MARKER TIMESTAMP
  tent.type = (unsigned short) 0x1c;
  tent.size = (unsigned short) 2;
  tent.addr = (addr_t) 0x002f3d56876b912a;  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
  
  //MARKER CPUID
  tent.type = (unsigned short) 0x1c;
  tent.size = (unsigned short) 3;
  tent.addr = (addr_t) 0;  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
  
  //FOOTER
  tent.type = (unsigned short) 0x1a;
  tent.size = (unsigned short) 0;
  tent.addr = (addr_t) 1 ; 
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 1);
  fclose(fptrace);
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
  
  trace_entry_t tent;
  
  Mcnt++;
  
  tent.type = (unsigned short) 0;
  tent.size = (unsigned short) bsize;
  tent.addr = (addr_t) addr;
  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
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
  
  trace_entry_t tent;
  
  Mcnt++;
  
  tent.type = (unsigned short) 1;
  tent.size = (unsigned short) bsize;
  tent.addr = (addr_t) addr;
  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);
}

// Set ROI flag
VOID StartROI() {
  
  isROI = true;
}

// Set ROI flag
VOID StopROI() {
  
  isROI = false; 
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
  
  trace_entry_t tent;
      
  Scatteredcnt++;
  
  for (UINT32 i = 0; i < memOpInfo->NumOfElements(); i++) {

    USIZE bsize =  memOpInfo->ElementSize(i);
    USIZE rw =  memOpInfo->ElementAccessType(i);
    addr_t addr = memOpInfo->ElementAddress(i);

    if (rw > 2)
      printf("ERROR: Scattered access has neither read or write\n");      
      
      
    tent.size = (unsigned short) bsize;
    tent.addr = (addr_t) addr;
    
    if ((rw == 0) || (rw == 2) ) {
      Mcnt++;
      //Mbytes += bsize;  
      tent.type = (unsigned short) 0;
      
      memcpy(ptrace, &tent, sizeof(trace_entry_t));
      ptrace += 1;	
      drtrace_write(fptrace, &btrace[0], &ptrace, 0);
    }
    
    if ((rw == 1) || (rw == 2) ) {
      Mcnt++;
      //Mbytes += bsize;  
      tent.type = (unsigned short) 1;
      
      memcpy(ptrace, &tent, sizeof(trace_entry_t));
      ptrace += 1;
      drtrace_write(fptrace, &btrace[0], &ptrace, 0);	
    }
  }
}


//VOID RecordInstr(VOID * ip, USIZE bsize, VOID * rtn) {
VOID RecordInstr(VOID * ip, USIZE bsize, THREADID threadid) {
    
      
  if (!isActive[threadid])
    return;
  
  gIcnt++;
  
  if ((gIcnt > ROI_A) && (gIcnt < ROI_B)) {
    if (do_print) {
      printf("PIN: ROI Start at %lu\n", gIcnt-1);
      fflush(stdout);
      do_print = false;
    }
    StartROI();
    
  } else {
    if (!do_print) {
      printf("PIN: ROI End at %lu\n", gIcnt-1);
      fflush(stdout);
      do_print = true;
    }
    StopROI();
  }
      
  if (!doTrace)
    return;
  
  if (( (gIcnt-1) % IINTER) == 0) {
    printf("PIN: %5.2f%c\n", 100.0 * (double) (gIcnt-1) / (double) ITOTAL, '%');
    fflush(stdout);
  }
  
  if (stopTrace)
    return;

  
  if(!isROI)
    return;
  
  trace_entry_t tent;
  Icnt++;
  
  tent.type = (unsigned short) 0xa;
  tent.size = (unsigned short) bsize;
  tent.addr = (addr_t) ip;
  
  memcpy(ptrace, &tent, sizeof(trace_entry_t));
  ptrace += 1;
  drtrace_write(fptrace, &btrace[0], &ptrace, 0);  

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
