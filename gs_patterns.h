
#pragma once

#include <exception>
#include <string>
#include <cstring>
#include <vector>

#define MAX(X, Y) (((X) < (Y)) ? Y : X)
#define MIN(X, Y) (((X) > (Y)) ? Y : X)
#define ABS(X) (((X) < 0) ? (-1) * (X) : X)

//triggers
#define SAMPLE 0
#define PERSAMPLE 10000000
//#define PERSAMPLE 1000

//info
#define CLSIZE (64)
#define NBUFS (1LL<<10)
#define IWINDOW (1024)
#define NGS (8096)

//patterns
#define USTRIDES 1024   //Threshold for number of accesses
#define NSTRIDES 15     //Threshold for number of unique distances
#define OUTTHRESH (0.5) //Threshold for percentage of distances at boundaries of histogram
#define NTOP (10)
#define INITIAL_PSIZE (1<<15)
#define MAX_PSIZE     (1<<30)

#define MAX_LINE_LENGTH 1024

namespace gs_patterns
{
    typedef uintptr_t addr_t;
    typedef enum { GATHER=0, SCATTER } mem_access_type;
    typedef enum { VECTOR=0, CTA } mem_instr_type;

    class GSError : public std::exception
    {
    public:
        GSError (const std::string & reason) : _reason(reason) { }
        ~GSError() {}

        const char * what() const noexcept override { return _reason.c_str(); }
    private:
        std::string _reason;
    };

    class GSFileError : public GSError
    {
    public:
        GSFileError (const std::string & reason) : GSError(reason) { }
        ~GSFileError() {}
    };

    class GSDataError : public GSError
    {
    public:
        GSDataError (const std::string & reason) : GSError(reason) { }
        ~GSDataError() {}
    };

    class GSAllocError : public GSError
    {
    public:
        GSAllocError (const std::string & reason) : GSError(reason) { }
        ~GSAllocError() {}
    };

    class InstrAddrAdapter
    {
    public:
        InstrAddrAdapter() { }
        virtual ~InstrAddrAdapter() { }

        virtual bool            is_valid() const            = 0;
        virtual bool            is_mem_instr() const        = 0;
        virtual bool            is_other_instr() const      = 0;
        virtual mem_access_type get_mem_access_type() const = 0;
        virtual mem_instr_type  get_mem_instr_type() const  = 0;

        virtual size_t         get_size() const             = 0;
        virtual addr_t         get_base_addr() const        = 0;
        virtual addr_t         get_address() const          = 0;
        virtual addr_t         get_iaddr() const            = 0;
        virtual addr_t         get_maddr() const            = 0;
        virtual unsigned short get_type() const             = 0; // must be 0 for GATHER, 1 for SCATTER !!
        virtual int64_t        get_max_access_size() const  = 0;

        virtual bool is_gather() const
        { return (is_valid() && is_mem_instr() && GATHER == get_mem_access_type()) ? true : false; }

        virtual bool is_scatter() const
        { return (is_valid() && is_mem_instr() && SCATTER == get_mem_access_type()) ? true : false; }

        virtual void output(std::ostream & os) const      = 0;
    };

    std::ostream & operator<<(std::ostream & os, const InstrAddrAdapter & ia);


    class Metrics
    {
    public:
        Metrics(mem_access_type mType) : _mType(mType), _pattern_sizes(NTOP)
        {
            try
            {
                for (int j = 0; j < NTOP; j++) {
                    patterns[j] = new int64_t[INITIAL_PSIZE];
                    _pattern_sizes[j] = INITIAL_PSIZE;
                }
            }
            catch (const std::exception & ex)
            {
                throw GSAllocError("Could not allocate patterns for " + type_as_string() + "! due to: " + ex.what());
            }
        }

        ~Metrics()
        {
            for (int i = 0; i < NTOP; i++) {
                delete [] patterns[i];
            }

            delete [] srcline;
        }

        size_t get_pattern_size(int pattern_index) {
            return _pattern_sizes[pattern_index];
        }

        bool grow(int pattern_index) {
            try {
                size_t old_size = _pattern_sizes[pattern_index];
                size_t new_size = old_size * 2;
                if (new_size > MAX_PSIZE) {
                    return false;
                }

                int64_t *tmp = new int64_t[new_size];
                memcpy(tmp, patterns[pattern_index], old_size * sizeof(int64_t));

                delete [] patterns[pattern_index];
                patterns[pattern_index] = tmp;
                _pattern_sizes[pattern_index] = new_size;

                return true;
            }
            catch (...) {
                return false;
            }
        }

        Metrics(const Metrics &) = delete;
        Metrics & operator=(const Metrics & right) = delete;

        std::string type_as_string() { return !_mType ? "GATHER" : "SCATTER"; }
        std::string getName()        { return !_mType ? "Gather" : "Scatter"; }
        std::string getShortName()   { return !_mType ? "G" : "S"; }

        auto get_srcline() { return srcline[_mType]; }

        int      ntop = 0;
        double   cnt = 0.0;
        int      offset[NTOP]  = {0};

        addr_t   tot[NTOP]     = {0};
        addr_t   top[NTOP]     = {0};
        addr_t   top_idx[NTOP] = {0};

        int64_t* patterns[NTOP] = {0};

    private:
        char (*srcline)[NGS][MAX_LINE_LENGTH] = new char[2][NGS][MAX_LINE_LENGTH];

        mem_access_type _mType;

        std::vector<size_t>  _pattern_sizes;
    };


    class InstrInfo
    {
    public:
        InstrInfo(mem_access_type mType) : _mType(mType) { }
        ~InstrInfo() {
            delete [] iaddrs;
            delete [] icnt;
            delete [] occ;
        }

        InstrInfo(const InstrInfo &) = delete;
        InstrInfo & operator=(const InstrInfo & right) = delete;

        addr_t*  get_iaddrs() { return iaddrs[_mType]; }
        int64_t* get_icnt()   { return icnt[_mType]; }
        int64_t* get_occ()    { return occ[_mType]; }

    private:
        addr_t (*iaddrs)[NGS] = new addr_t[2][NGS];
        int64_t (*icnt)[NGS]  = new int64_t[2][NGS];
        int64_t (*occ)[NGS]   = new int64_t[2][NGS];

        mem_access_type _mType;
    };

    class TraceInfo  // Stats
    {
    public:
        /// TODO: need a reset method to zero out counters

        uint64_t opcodes     = 0;
        uint64_t opcodes_mem = 0;
        uint64_t addrs       = 0;
        uint64_t other       = 0;
        int64_t  ngs         = 0;
        int64_t trace_lines  = 0;

        bool    did_opcode      = false; // revist this ---------------
        double  other_cnt       = 0.0;
        double  gather_score    = 0.0;
        double  gather_occ_avg  = 0.0;
        double  scatter_occ_avg = 0.0;

        uint64_t     mcnt  = 0;
    };

    template <std::size_t MAX_ACCESS_SIZE>
    class InstrWindow
    {
    public:
        InstrWindow() {
            // First dimension is 0=GATHER/1=SCATTER
            _w_iaddrs = new int64_t[2][IWINDOW];
            _w_bytes  = new int64_t[2][IWINDOW];
            _w_maddr  = new int64_t[2][IWINDOW][MAX_ACCESS_SIZE];
            _w_cnt    = new int64_t[2][IWINDOW];

            init();
        }

        virtual ~InstrWindow() {
            delete [] _w_iaddrs;
            delete [] _w_bytes;
            delete [] _w_maddr;
            delete [] _w_cnt;
        }

        void init() {
            for (int w = 0; w < 2; w++) {
                for (int i = 0; i < IWINDOW; i++) {
                    _w_iaddrs[w][i] = -1;
                    _w_bytes[w][i] = 0;
                    _w_cnt[w][i] = 0;
                    for (uint64_t j = 0; j < MAX_ACCESS_SIZE; j++)
                        _w_maddr[w][i][j] = -1;
                }
            }
        }

        void reset(int w) {
            for (int i = 0; i < IWINDOW; i++) {
                _w_iaddrs[w][i] = -1;
                _w_bytes[w][i] = 0;
                _w_cnt[w][i] = 0;
                for (uint64_t j = 0; j < MAX_ACCESS_SIZE; j++)
                    _w_maddr[w][i][j] = -1;
            }
        }

        void reset() {
            for (int w = 0; w < 2; w++) {
                reset(w);
            }
        }

        InstrWindow(const InstrWindow &) = delete;
        InstrWindow & operator=(const InstrWindow & right) = delete;

        int64_t & w_iaddrs(int32_t i, int32_t j)             { return _w_iaddrs[i][j];   }
        int64_t & w_bytes(int32_t i, int32_t j)              { return _w_bytes[i][j];    }
        int64_t & w_maddr(int32_t i, int32_t j, int32_t k)   { return _w_maddr[i][j][k]; }
        int64_t & w_cnt(int32_t i, int32_t j)                { return _w_cnt[i][j];      }

        addr_t &  get_iaddr()       { return iaddr;      }
        int64_t & get_maddr_prev()  { return maddr_prev; }
        int64_t & get_maddr()       { return maddr;      }

    private:
        // First dimension is 0=GATHER/1=SCATTER
        int64_t (*_w_iaddrs)[IWINDOW];
        int64_t (*_w_bytes)[IWINDOW];
        int64_t (*_w_maddr)[IWINDOW][MAX_ACCESS_SIZE];
        int64_t (*_w_cnt)[IWINDOW];

        // State which must be carried with each call to handle a trace
        addr_t   iaddr;
        int64_t  maddr_prev;
        int64_t  maddr;
    };

    template <std::size_t MAX_ACCESS_SIZE>
    class MemPatterns
    {
    public:
        MemPatterns() { }
        virtual ~MemPatterns() { };

        MemPatterns(const MemPatterns &) = delete;
        MemPatterns & operator=(const MemPatterns &) = delete;

        virtual void handle_trace_entry(const InstrAddrAdapter & ia) = 0;
        virtual void generate_patterns() = 0;

        virtual Metrics &     get_metrics(mem_access_type) = 0;
        virtual InstrInfo &   get_iinfo(mem_access_type)   = 0;

        virtual Metrics &     get_gather_metrics()      = 0;
        virtual Metrics &     get_scatter_metrics()     = 0;
        virtual InstrInfo &   get_gather_iinfo()        = 0;
        virtual InstrInfo &   get_scatter_iinfo()       = 0;
        virtual TraceInfo &   get_trace_info()          = 0;
        virtual InstrWindow<MAX_ACCESS_SIZE> &
                              get_instr_window()        = 0;
        virtual void          set_log_level(int8_t ll)  = 0;
        virtual int8_t        get_log_level()           = 0;
    };

} // namespace gs_patterns
