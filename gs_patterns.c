#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <zlib.h>
#include <sys/resource.h>

#define MAX(X, Y) (((X) < (Y)) ? Y : X)
#define MIN(X, Y) (((X) > (Y)) ? Y : X)
#define ABS(X) (((X) < 0) ? (-1) * (X) : X)

//triggers
#define SAMPLE 0
#define PERSAMPLE 10000000
//#define PERSAMPLE 1000

//info
#define CLSIZE (64)
#define VBITS (512)
#define NBUFS (1LL<<10)
#define IWINDOW (1024)
#define NGS (8096)

//patterns
#define USTRIDES 1024   //Threshold for number of accesses
#define NSTRIDES 15     //Threshold for number of unique distances
#define OUTTHRESH (0.5) //Threshold for percentage of distances at boundaries of histogram
#define NTOP (10)
#define PSIZE (1<<23)
//#define PSIZE (1<<18)

//DONT CHANGE
#define VBYTES (VBITS/8)

//Terminal colors
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"

//address status
#define ADDREND   (0xFFFFFFFFFFFFFFFFUL)
#define ADDRUSYNC (0xFFFFFFFFFFFFFFFEUL)

typedef uintptr_t addr_t;

//FROM DR SOURCE
//DR trace
struct _trace_entry_t {
  unsigned short type; // 2 bytes: trace_type_t
  unsigned short size;
  union {
    addr_t addr; 
    unsigned char length[sizeof(addr_t)];
  };
}  __attribute__((packed));
typedef struct _trace_entry_t trace_entry_t;

static inline int popcount(uint64_t x) {
    int c;
    
    for (c = 0; x != 0; x >>= 1)
      if (x & 1)
	c++;
    return c;
}

//string tools
int startswith(const char *a, const char *b) {
  if(strncmp(b, a, strlen(b)) == 0)
    return 1;
  return 0;
}

int endswith(const char *a, const char *b) {
  int idx = strlen(a);
  int preidx = strlen(b);

  if (preidx >= idx)
    return 0;
  if(strncmp(b, &a[idx-preidx], preidx) == 0)
    return 1;
  return 0;
}

//https://stackoverflow.com/questions/779875/what-function-is-to-replace-a-substring-from-a-string-in-c
char *str_replace(char *orig, char *rep, char *with) {
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig)
        return NULL;
    
    if (!rep)
      return orig;
    
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; tmp = strstr(ins, rep); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

char * get_str(char * line, char * bparse, char * aparse) {

  char * sline;

  sline = str_replace(line, bparse, "");
  sline = str_replace(sline, aparse, "");

  return sline;
}

int cnt_str(char * line, char c) {

  int cnt = 0;
  for(int i=0; line[i] != '\0'; i++){
    if (line[i] == c)
      cnt++;
  }

  return cnt;
}
  
void translate_iaddr(char * binary, char * source_line, addr_t iaddr) {

  int i = 0;
  int ntranslated = 0;
  char path[1024];
  char cmd[1024];
  FILE *fp;
    
  sprintf(cmd, "addr2line -e %s 0x%lx", binary, iaddr);
    
  /* Open the command for reading. */
  fp = popen(cmd, "r");
  if (fp == NULL) {
    printf("Failed to run command\n" );
    exit(1);
  }
  
  /* Read the output a line at a time - output it. */
  while (fgets(path, sizeof(path), fp) != NULL) {
    strcpy(source_line, path);
    source_line[strcspn(source_line, "\n")] = 0;
  }
  
  /* close */
  pclose(fp);
  
  return;
}

int drline_read(gzFile fp, trace_entry_t * val, trace_entry_t ** p_val, int * edx) {

  int idx;

  idx = (*edx)/sizeof(trace_entry_t);
  //first read
  if (*p_val == NULL)  {
    *edx = gzread(fp, val, sizeof(trace_entry_t)*NBUFS);
    *p_val = val;
      
  } else if (*p_val == &val[idx]) {
    *edx = gzread(fp, val, sizeof(trace_entry_t)*NBUFS);
    *p_val = val;    
  }
  
  if (*edx == 0) 
    return 0;
  
  return 1;
}

int main(int argc, char ** argv) {

  //generic
  int i, j, k, m, n, w;
  int iwindow = 0;
  int iret = 0;
  int ret;
  int did_opcode = 0;
  int windowfull = 0;
  int byte;
  int do_gs_traces = 0;
  int do_filter = 1;
  int64_t ngs = 0;  
  char *eptr;
  char binary[1024];
  char srcline[1024];
  
  //dtrace vars
  int64_t drtrace_lines = 0;
  trace_entry_t * drline;
  trace_entry_t * drline2;
  trace_entry_t * p_drtrace = NULL; 
  trace_entry_t drtrace[NBUFS];
  gzFile fp_drtrace;
  FILE * fp_gs;

  //metrics
  int gs;
  uint64_t opcodes = 0;
  uint64_t opcodes_mem = 0;
  uint64_t addrs = 0;
  uint64_t other = 0;
  int64_t maddr_prev;
  int64_t maddr;
  int64_t mcl;
  int64_t gather_bytes_hist[100] = {0};
  int64_t scatter_bytes_hist[100] = {0};
  double gather_cnt = 0.0;
  double scatter_cnt = 0.0;
  double other_cnt = 0.0;
  double gather_score = 0.0;
  double gather_occ_avg = 0.0;
  double scatter_occ_avg = 0.0;

  //windows
  int w_rw_idx;
  int w_idx;
  addr_t iaddr;
  int64_t w_iaddrs[2][IWINDOW];
  int64_t w_bytes[2][IWINDOW];
  int64_t w_maddr[2][IWINDOW][VBYTES];
  int64_t w_cnt[2][IWINDOW];

  //First pass to find top gather / scatters
  char gather_srcline[NGS][1024];
  addr_t gather_iaddrs[NGS] = {0};
  int64_t gather_icnt[NGS] = {0};
  int64_t gather_occ[NGS] = {0};
  char scatter_srcline[NGS][1024];
  addr_t scatter_iaddrs[NGS] = {0};
  int64_t scatter_icnt[NGS] = {0};
  int64_t scatter_occ[NGS] = {0};

  //Second Pass
  int dotrace;
  int bestcnt;
  int bestidx;
  int gather_ntop = 0;
  int scatter_ntop = 0;
  int gather_offset[NTOP] = {0};
  int scatter_offset[NTOP] = {0};
  
  addr_t best_iaddr;
  addr_t gather_tot[NTOP] = {0};
  addr_t scatter_tot[NTOP] = {0};
  addr_t gather_top[NTOP] = {0};
  addr_t gather_top_idx[NTOP] = {0};
  addr_t scatter_top[NTOP] = {0};
  addr_t scatter_top_idx[NTOP] = {0};
  addr_t gather_base[NTOP] = {0};
  addr_t scatter_base[NTOP] = {0};
  int64_t * gather_patterns[NTOP] = {0};
  int64_t * scatter_patterns[NTOP] = {0};

  for(j=0; j<NTOP; j++) {
    gather_patterns[j] = (int64_t *) calloc(PSIZE, sizeof(int64_t));
    if (gather_patterns[j] == NULL) {
      printf("ERROR: Could not allocate gather_patterns!\n");
      exit(-1);
    }
  }
  
  for(j=0; j<NTOP; j++) {
    scatter_patterns[j] = (int64_t *) calloc(PSIZE, sizeof(int64_t));
    if (scatter_patterns[j] == NULL) {
      printf("ERROR: Could not allocate scatter_patterns!\n");
      exit(-1);
    }
  }
  
  if (argc == 3) {
        
    // 1 open dr trace
    fp_drtrace = gzopen(argv[1], "hrb");
    if (fp_drtrace == NULL) {
      printf("ERROR: Could not open %s!\n", argv[1]);
      exit(-1);
    }

    strcpy(binary, argv[2]);
    
  } else {
    printf("ERROR: Invalid arguments, should be: trace.gz binary\n");
    exit(-1);	
  }

  //init window arrays
  for(w=0; w<2; w++) {
    for (i = 0; i < IWINDOW; i++) {
      w_iaddrs[w][i] = -1;
      w_bytes[w][i] = 0;
      w_cnt[w][i] = 0;
      for (j=0; j<VBYTES; j++)
	w_maddr[w][i][j] = -1;
    }
  }

  uint64_t mcnt = 0;
  uint64_t unique_iaddrs = 0;
  int unsynced = 0;
  uint64_t unsync_cnt = 0;
  addr_t ciaddr;

  printf("First pass to find top gather / scatter iaddresses\n"); fflush(stdout);
	 
  //read dr trace entries instrs
  //printf("%16s %16s %16s %16s %16s %16s\n", "iaddr", "rw", "byte", "bytes", "cnt", "maddr");
  while ( drline_read(fp_drtrace, drtrace, &p_drtrace, &iret) ) {
    
    //decode drtrace
    drline = p_drtrace;
    
    /*****************************/
    /** INSTR 0xa-0x10 and 0x1e **/
    /*****************************/
    if ( ((drline->type >= 0xa) && (drline->type <= 0x10)) || (drline->type == 0x1e) ) {
      
      //iaddr
      iaddr = drline->addr;
      
      //nops
      opcodes++;
      did_opcode = 1;
      
      /***********************/
      /** MEM 0x00 and 0x01 **/
      /***********************/
    } else if ( (drline->type == 0x0) || (drline->type == 0x1) ) {

      w_rw_idx = drline->type;

      //printf("M DRTRACE -- iaddr: %016lx addr: %016lx cl_start: %d bytes: %d\n",
      //     iaddr,  drline->addr, drline->addr % 64, drline->size);

      if ((++mcnt % PERSAMPLE) == 0) {
#if SAMPLE
	break;
#endif
	printf(".");
	fflush(stdout);
      }
      
      //is iaddr in window
      w_idx = -1;
      for (i=0; i<IWINDOW; i++) {
	
	//new iaddr
	if (w_iaddrs[w_rw_idx][i] == -1) {
	  w_idx = i;
	  break;
	  
	  //iaddr exists
	} else if (w_iaddrs[w_rw_idx][i] == iaddr) {
	  w_idx = i;
	  break	;  
	}
      }
      
      //new window
      if ( (w_idx == -1) || (w_bytes[w_rw_idx][w_idx] >= VBYTES) ||
	   (w_cnt[w_rw_idx][w_idx] >= VBYTES) ) {
	
	/***************************/
	//do analysis
	/***************************/
	//i = each window
	for(w=0; w<2; w++) {
	  
	  for (i=0; i<IWINDOW; i++) {
	  
	    if (w_iaddrs[w][i] == -1)
	      break;
	  
	    byte = w_bytes[w][i] / w_cnt[w][i];
	  
	    //First pass
	    //Determine
	    //gather/scatter?
	    gs = -1;
	    for (j=0; j<w_cnt[w][i]; j++) {

	      //address and cl
	      maddr = w_maddr[w][i][j];
	      assert(maddr > -1);
	      
	      //previous addr
	      if (j==0)
		maddr_prev = maddr - 1;
	      
	      //gather / scatter
	      if ( maddr != maddr_prev) {
		if ( (gs == -1) && (abs(maddr - maddr_prev) > 1) )
		  gs = w;
	      }	      
	      maddr_prev = maddr;
	    }
	    
	    for (j=0; j<w_cnt[w][i]; j++) {
	      
	      if (gs == -1) {
		other_cnt++;
		continue;
	      }
	    }
	    	    
	    if (gs == 0) {
	      
	      gather_occ_avg += w_cnt[w][i];
	      gather_cnt += 1.0;
	      
	      for(k=0; k<NGS; k++) {
		if (gather_iaddrs[k] == 0) {
		  gather_iaddrs[k] = w_iaddrs[w][i];
		  gather_icnt[k]++;
		  gather_occ[k] += w_cnt[w][i];
		  break;
		}
		
		if (gather_iaddrs[k] == w_iaddrs[w][i]) {
		  gather_icnt[k]++;
		  gather_occ[k] += w_cnt[w][i];
		  break;
		}
		
	      }
	      
	    } else if (gs == 1) {
	      
	      scatter_occ_avg += w_cnt[w][i];
	      scatter_cnt += 1.0;
	      
	      for(k=0; k<NGS; k++) {
		if (scatter_iaddrs[k] == 0) {
		  scatter_iaddrs[k] = w_iaddrs[w][i];
		  scatter_icnt[k]++;
		  scatter_occ[k] += w_cnt[w][i];
		  break;
		}
		
		if (scatter_iaddrs[k] == w_iaddrs[w][i]) {
		  scatter_icnt[k]++;
		  scatter_occ[k] += w_cnt[w][i];
		  break;
		}		
	      }
	    }
	  } //WINDOW i
	
	  w_idx = 0;
	  
	  //reset windows
	  for (i = 0; i < IWINDOW; i++) {
	    w_iaddrs[w][i] = -1;
	    w_bytes[w][i] = 0;
	    w_cnt[w][i] = 0;
	    for (j=0; j<VBYTES; j++)
	      w_maddr[w][i][j] = -1;	
	  }	  
	} // rw w
      } //analysis
            
      //Set window values
      w_iaddrs[w_rw_idx][w_idx] = iaddr;
      w_maddr[w_rw_idx][w_idx][w_cnt[w_rw_idx][w_idx]] = drline->addr / drline->size;
      w_bytes[w_rw_idx][w_idx] += drline->size;    
      
      //num access per iaddr in loop
      w_cnt[w_rw_idx][w_idx]++;      
      
      if (did_opcode) {	
	
	opcodes_mem++;
	addrs++;
	did_opcode = 0;
	
      } else {
	addrs++;
      }
      
      /***********************/
      /** SOMETHING ELSE **/
      /***********************/
    } else {
      other++;
    }
    
    p_drtrace++;
    drtrace_lines++;
    
  } //while drtrace
  
  //metrics
  gather_occ_avg /= gather_cnt;
  scatter_occ_avg /= scatter_cnt;

  printf("\n RESULTS \n");
  
  //close files
  gzclose(fp_drtrace);
  
  printf("DRTRACE STATS\n");
  printf("DRTRACE LINES:        %16lu\n", drtrace_lines);
  printf("OPCODES:              %16lu\n", opcodes);
  printf("MEMOPCODES:           %16lu\n", opcodes_mem);
  printf("LOAD/STORES:          %16lu\n", addrs);
  printf("OTHER:                %16lu\n", other);
  
  printf("\n");
  
  printf("GATHER/SCATTER STATS: \n");
  printf("LOADS per GATHER:     %16.3f\n", gather_occ_avg);
  printf("STORES per SCATTER:   %16.3f\n", scatter_occ_avg);
  printf("GATHER COUNT:         %16.3f (log2)\n", log(gather_cnt) / log(2.0));
  printf("SCATTER COUNT:        %16.3f (log2)\n", log(scatter_cnt) / log(2.0));
  printf("OTHER  COUNT:         %16.3f (log2)\n", log(other_cnt) / log(2.0));
  
  //Find source lines 

  //Must have symbol
  printf("\nSymbol table lookup for gathers..."); fflush(stdout);
  gather_cnt = 0.0;
  for(k=0; k<NGS; k++) {
    
    if (gather_iaddrs[k] == 0)
      break;
    
    translate_iaddr(binary, gather_srcline[k], gather_iaddrs[k]);
    
    if (startswith(gather_srcline[k], "?"))
      gather_icnt[k] = 0;
    
    gather_cnt += gather_icnt[k];
  }
  printf("done.\n");
  //printf("\nTOP GATHERS\n");

  //Get top gathers
  gather_ntop = 0;
  for(j=0; j<NTOP; j++) {
    
    bestcnt = 0;
    best_iaddr = 0;
    bestidx = -1;
    
    for(k=0; k<NGS; k++) {

      if (gather_icnt[k] == 0)
	continue;

      if (gather_iaddrs[k] == 0) {
	break;
      }
      
      if (gather_icnt[k] > bestcnt) {
	bestcnt = gather_icnt[k];
	best_iaddr =  gather_iaddrs[k];
	bestidx = k;
      }
      
    }
    
    if (best_iaddr == 0) {
      break;
      
    } else {

      gather_ntop++;
      //printf("GIADDR -- %016lx: %16lu -- %s\n",
      //     gather_iaddrs[bestidx], gather_icnt[bestidx], gather_srcline[bestidx]);
      
      gather_top[j] = best_iaddr;
      gather_top_idx[j] = bestidx;
      gather_tot[j] = gather_icnt[bestidx];  
      gather_icnt[bestidx] = 0;
      
    }
  }

  //Find source lines  
  scatter_cnt = 0.0;
  
  printf("Symbol table lookup for scatters..."); fflush(stdout);
  //Check it is not a library
  for(k=0; k<NGS; k++) {
    
    if (scatter_iaddrs[k] == 0) {
      break;
    }
    translate_iaddr(binary, scatter_srcline[k], scatter_iaddrs[k]);
    if (startswith(scatter_srcline[k], "?"))
      scatter_icnt[k] = 0;
    
    scatter_cnt += scatter_icnt[k];
  }
  printf("done.\n");
  
  //Get top scatters    
  //printf("\nTOP SCATTERS\n");
  scatter_ntop = 0;
  for(j=0; j<NTOP; j++) {
    
    bestcnt = 0;
    best_iaddr = 0;
    bestidx = -1;
    
    for(k=0; k<NGS; k++) {

      if (scatter_icnt[k] == 0)
	continue;
      
      if (scatter_iaddrs[k] == 0) {
	break;
      }
      
      if (scatter_icnt[k] > bestcnt) {
	bestcnt = scatter_icnt[k];
	best_iaddr =  scatter_iaddrs[k];
	bestidx = k;
      }      
    }
    
    if (best_iaddr == 0) {
      break;
      
    } else {

      scatter_ntop++;      
      scatter_top[j] = best_iaddr;
      scatter_top_idx[j] = bestidx;
      scatter_tot[j] = scatter_icnt[bestidx];
      scatter_icnt[bestidx] = 0;
      //printf("SIADDR -- %016lx: %16lu -- %s\n",
      //     scatter_top[j], scatter_tot[j], scatter_srcline[bestidx]);
    }
  }
  
  //Second Pass
  
  //Open trace
  fp_drtrace = gzopen(argv[1], "hrb");
  if (fp_drtrace == NULL) {
    printf("ERROR: Could not open %s!\n", argv[1]);
    exit(-1);
  }    
  
  mcnt = 0;
  iret = 0;
  p_drtrace = NULL;
  int breakout = 0;
  printf("\nSecond pass to fill gather / scatter subtraces\n"); fflush(stdout);
  while ( drline_read(fp_drtrace, drtrace, &p_drtrace, &iret) && !breakout ) {
    
    //decode drtrace
    drline = p_drtrace;
    
    /*****************************/
    /** INSTR 0xa-0x10 and 0x1e **/
    /*****************************/
    if ( ((drline->type >= 0xa) && (drline->type <= 0x10)) || (drline->type == 0x1e) ) {
      
      //iaddr
      iaddr = drline->addr;
      
      
      /***********************/
      /** MEM 0x00 and 0x01 **/
      /***********************/
    } else if ( (drline->type == 0x0) || (drline->type == 0x1) ) {
      
      maddr = drline->addr / drline->size;
      
      if ((++mcnt % PERSAMPLE) == 0) {
#if SAMPLE
	break;
#endif
	printf(".");
	fflush(stdout);
      }
      
      //gather ?
      if (drline->type == 0x0) {
	
	for(i=0; i<gather_ntop; i++) {
	  
	  //found it
	  if (iaddr == gather_top[i]) {
	    
	    if (gather_base[i] == 0)
	      gather_base[i] = maddr;
	    
	    //Add index
	    if (gather_offset[i] >= PSIZE) {
	      printf("WARNING: Need to increase PSIZE. Truncating trace...\n");
	      breakout = 1;
	    }
	    //printf("g -- %d % d\n", i, gather_offset[i]); fflush(stdout);
	    gather_patterns[i][ gather_offset[i]++ ] = (int64_t) (maddr - gather_base[i]);	
	    
	    break;
	  }
	}
	
	//scatter ?
      } else {
	
	for(i=0; i<scatter_ntop; i++) {
	  
	  //found it
	  if (iaddr == scatter_top[i]) {
	    
	    //set base
	    if (scatter_base[i] == 0) 
	      scatter_base[i] =  maddr;
	    
	    //Add index
	    if (scatter_offset[i] >= PSIZE) {
	      printf("WARNING: Need to increase PSIZE. Truncating trace...\n");
	      breakout = 1;
	    }
	    scatter_patterns[i][ scatter_offset[i]++ ] = (int64_t) (maddr - scatter_base[i]);
	    break;	      
	  }
	}
      }
      
    } //MEM
    
    p_drtrace++;
    
  } //while drtrace
  
  gzclose(fp_drtrace);

  printf("\n");
  
  //Normalize
  int64_t smallest;
  for(i=0; i<gather_ntop; i++) {
    
    //Find smallest
    smallest = 0;
    for(j=0; j<gather_offset[i]; j++) {
      if (gather_patterns[i][j] < smallest)
	smallest = gather_patterns[i][j];
    }

    smallest *= -1;
    //Normalize
    for(j=0; j<gather_offset[i]; j++) {
      gather_patterns[i][j] += smallest;    
    }
  }
  
  for(i=0; i<scatter_ntop; i++) {
    
    //Find smallest
    smallest = 0;
    for(j=0; j<scatter_offset[i]; j++) {
      if (scatter_patterns[i][j] < smallest)
	smallest = scatter_patterns[i][j];
    }
    smallest *= -1;
    
    //Normalize
    for(j=0; j<scatter_offset[i]; j++) {
      scatter_patterns[i][j] += smallest;    
    }
  }    

  //Create stride histogram and create spatter
  int sidx;
  int first_spatter = 1;
  int unique_strides;
  int64_t idx, pidx;
  int64_t n_stride[1027];
  double outbounds;
  //print


  //Create spatter file
  FILE * fp, * fp2;
  char * json_name, * gs_info;
  json_name = str_replace(argv[1], ".gz", ".json"); 
  fp = fopen(json_name, "w");
  if (fp == NULL) {
    printf("ERROR: Could not open %s!\n", json_name);
    exit(-1);
  }
  gs_info = str_replace(argv[1], ".gz", ".txt");
  fp2 = fopen(gs_info, "w");
  if (fp2 == NULL) {
    printf("ERROR: Could not open %s!\n", gs_info);
    exit(-1);
  }

  //Header
  fprintf(fp, "[ ");
  fprintf(fp2, "#sourceline, g/s, indices, percentage of g/s in trace\n");
  
  printf("\n");
  for(i=0; i<gather_ntop; i++) {
    printf("***************************************************************************************\n");

    unique_strides = 0;
    for(j=0; j<1027; j++)
      n_stride[j] = 0;
    
    for(j=1; j<gather_offset[i]; j++) {
      sidx = gather_patterns[i][j] - gather_patterns[i][j-1] + 513;
      sidx = (sidx < 1) ? 0 : sidx;
      sidx = (sidx > 1025) ? 1026 : sidx;
      n_stride[sidx]++;
    }

    for(j=0; j<1027; j++) {
      if (n_stride[j] > 0) {
	unique_strides++;
      }
    }

    outbounds = (double) (n_stride[0] + n_stride[1026]) / (double) gather_offset[i];
    
    //if ( ( (unique_strides > NSTRIDES) || (outbounds > OUTTHRESH) ) && (gather_offset[i] > USTRIDES ) ){
    if (1) {

      //create a binary file
      FILE * fp_bin;
      char * bin_name;
      bin_name = str_replace(argv[1], ".gz", ".sbin");
      printf("%s\n", bin_name);
      fp_bin = fopen(bin_name, "w");
      if (fp_bin == NULL) {
	printf("ERROR: Could not open %s!\n", bin_name);
	exit(-1);
      }
      
      printf("GIADDR   -- %p\n",  gather_top[i]);
      printf("SRCLINE  -- %s\n", gather_srcline[ gather_top_idx[i] ] );
      printf("GATHER %c -- %6.3f%c (512-bit chunks)\n",
	     '%', 100.0 * (double) gather_tot[i] / gather_cnt, '%');
      printf("NDISTS  -- %ld\n", gather_offset[i]);
      
      int64_t nlcnt = 0;
      for(j=0; j<gather_offset[i]; j++) {
	
	if (j < 39) {
	  printf("%10ld ", gather_patterns[i][j]); fflush(stdout);
	  if (( ++nlcnt % 13) == 0)
	    printf("\n");
	  
	} else if (j >= (gather_offset[i] - 39)) {
	  printf("%10ld ", gather_patterns[i][j]); fflush(stdout);
	  if (( ++nlcnt % 13) == 0)
	    printf("\n");
	  
	} else if (j == 39)
	  printf("...\n");
      }    
      printf("\n");
      printf("DIST HISTOGRAM --\n");
      
      for(j=0; j<1027; j++) {
	if (n_stride[j] > 0) {
	  if (j == 0)
	    printf("%6s: %ld\n", "< -512", n_stride[j]);
	  else if (j == 1026)
	    printf("%6s: %ld\n", ">  512", n_stride[j]);
	  else
	    printf("%6d: %ld\n", j-513, n_stride[j]);
	}
      }
      
      if (first_spatter) {
	first_spatter = 0;
	fprintf(fp, " {\"kernel\":\"Gather\", \"pattern\":[");
		
      } else {
	fprintf(fp, ",\n {\"kernel\":\"Gather\", \"pattern\":[");
      }

      fwrite(gather_patterns[i], sizeof(uint64_t), gather_offset[i], fp_bin);
      fclose(fp_bin);
      
      for(j=0; j<gather_offset[i]-1; j++)	
	fprintf(fp, "%ld,",  gather_patterns[i][j]);
      fprintf(fp, "%ld",  gather_patterns[i][gather_offset[i]-1]);

      fprintf(fp, "], \"count\":1}");

      fprintf(fp2, "%s,G,%ld,%6.3f\n",
	      gather_srcline[ gather_top_idx[i] ], gather_offset[i],
	      100.0*(double) gather_tot[i] / gather_cnt);
    }   
    printf("***************************************************************************************\n\n"); 
  }
  
  printf("\n");
  for(i=0; i<scatter_ntop; i++) {
    printf("***************************************************************************************\n");
    
    unique_strides = 0;
    for(j=0; j<1027; j++)
      n_stride[j] = 0;
    
    for(j=1; j<scatter_offset[i]; j++) {
      sidx = scatter_patterns[i][j] - scatter_patterns[i][j-1] + 513;
      sidx = (sidx < 1) ? 0 : sidx;
      sidx = (sidx > 1025) ? 1026 : sidx;
      n_stride[sidx]++;
    }
      
    for(j=0; j<1027; j++) {
      if (n_stride[j] > 0) {
	unique_strides++;
      }
    }
    
    outbounds = (double) (n_stride[0] + n_stride[1026]) / (double) scatter_offset[i];
    
    //if (((unique_strides > NSTRIDES)  | (outbounds > OUTTHRESH) ) && (scatter_offset[i] > USTRIDES) ){
    if (1) {
      
      //create a binary file
      FILE * fp_bin;
      char * bin_name;
      bin_name = str_replace(argv[1], ".gz", ".sbin");
      printf("%s\n", bin_name);
      fp_bin = fopen(bin_name, "w");
      if (fp_bin == NULL) {
	printf("ERROR: Could not open %s!\n", bin_name);
	exit(-1);
      }
      
      printf("SIADDR    -- %p\n",   scatter_top[i]);
      printf("SRCLINE   -- %s\n", scatter_srcline[ scatter_top_idx[i]]);
      printf("SCATTER %c -- %6.3f%c (512-bit chunks)\n",
	     '%', 100.0 * (double) scatter_tot[i] / scatter_cnt, '%');
      printf("NDISTS  -- %ld\n", scatter_offset[i]);
      
      int64_t nlcnt = 0;
      for(j=0; j<scatter_offset[i]; j++) {
	
	if (j < 39) {
	  printf("%10ld ", scatter_patterns[i][j]); fflush(stdout);
	  if (( ++nlcnt % 13) == 0)
	    printf("\n");
	  
	} else if (j >= (scatter_offset[i] - 39)) {
	  printf("%10ld ", scatter_patterns[i][j]); fflush(stdout);
	  if (( ++nlcnt % 13) == 0)
	    printf("\n");
	  
	} else if (j == 39)
	  printf("...\n");
      }
      printf("\n");
      printf("DIST HISTOGRAM --\n");
      for(j=0; j<1027; j++) {
	if (n_stride[j] > 0) {
	  if (j == 0)
	    printf("%6s: %ld\n", "< -512", n_stride[j]);
	  else if (j == 1026)
	    printf("%6s: %ld\n", ">  512", n_stride[j]);
	  else
	    printf("%6d: %ld\n", j-513, n_stride[j]);
	}
      }
      
      if (first_spatter) {
	first_spatter = 0;
	fprintf(fp, " {\"kernel\":\"Scatter\", \"pattern\":[");
		
      } else {
	fprintf(fp, ", {\"kernel\":\"Scatter\", \"pattern\":[");
      }

      fwrite(scatter_patterns[i], sizeof(uint64_t), scatter_offset[i], fp_bin);
      fclose(fp_bin);
      
      for(j=0; j<scatter_offset[i]-1; j++)	
	fprintf(fp, "%ld,",  scatter_patterns[i][j]);
      fprintf(fp, "%ld",  scatter_patterns[i][scatter_offset[i]-1]);
      fprintf(fp, "], \"count\":1}");
      
      fprintf(fp2, "%s,S,%ld,%6.3f\n",
	      scatter_srcline[ scatter_top_idx[i] ], scatter_offset[i],
	      100.0*(double) scatter_tot[i] / scatter_cnt);
    }    
    printf("***************************************************************************************\n\n");
  }

  //Footer
  fprintf(fp, " ]");
  fclose(fp);
  fclose(fp2);
  
  
  for(i=0; i<NTOP; i++) {
    free(gather_patterns[i]);
    free(scatter_patterns[i]);
  }

  
  return 0;
  
}
