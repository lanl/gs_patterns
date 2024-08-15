#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <string>
#include <exception>

#include "gs_patterns.h"
#include "gs_patterns_core.h"
#include "utils.h"

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

#define VBITS (512)
#define VBYTES (VBITS/8) //DONT CHANGE

namespace gs_patterns
{
namespace gspin_patterns
{
    constexpr std::size_t MEMORY_ACCESS_SIZE = VBYTES;

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

    // An adapter for trace_entry_t
    class InstrAddrAdapterForPin : public InstrAddrAdapter
    {
    public:
        InstrAddrAdapterForPin(const trace_entry_t * te)
        {
            /// TODO: do we need to copy this, will we outlive trace_entry_t which is passed in ?
            _te.type = te->type;
            _te.size = te->size;
            _te.addr = te->addr;
        }
        InstrAddrAdapterForPin(const trace_entry_t te) : _te(te) { }

        virtual ~InstrAddrAdapterForPin() { }

        virtual inline bool            is_valid() const override            { return !(0 == _te.type && 0 == _te.size);        }
        virtual inline bool            is_mem_instr() const override        { return ((_te.type == 0x0) || (_te.type == 0x1)); }
        virtual inline bool            is_other_instr() const override      { return ((_te.type >= 0xa) && (_te.type <= 0x10)) || (_te.type == 0x1e); }

        virtual mem_access_type get_mem_access_type() const override        {
            if (!is_mem_instr()) throw GSDataError("Not a Memory Instruction - unable to determine Access Type");
            // Must be 0x0 or 0x1
            if (_te.type == 0x0) return GATHER;
            else return SCATTER;
        }
        virtual inline mem_instr_type  get_mem_instr_type() const override  { return VECTOR;   }

        virtual inline size_t          get_size() const override            { return _te.size; }
        virtual inline addr_t          get_base_addr() const override       { return _te.addr; }
        virtual inline addr_t          get_address() const override         { return _te.addr; }
        virtual inline addr_t          get_iaddr() const override           { return _te.addr; }
        virtual inline addr_t          get_maddr() const override           { return _te.addr / _te.size; }
        virtual inline unsigned short  get_type() const override            { return _te.type; } // must be 0 for GATHER, 1 for SCATTER !!
        virtual inline int64_t         get_max_access_size() const override { return MEMORY_ACCESS_SIZE;  }

        virtual void output(std::ostream & os) const override {
            os << "InstrAddrAdapterForPin: trace entry: type: [" << _te.type << "] size: [" << _te.size << "]";
        }

    private:
        trace_entry_t _te;
    };

    class MemPatternsForPin : public MemPatterns<MEMORY_ACCESS_SIZE>
    {
    public:
        MemPatternsForPin() : _metrics(GATHER, SCATTER),
                              _iinfo(GATHER, SCATTER) { }
        virtual ~MemPatternsForPin() override { }

        void handle_trace_entry(const InstrAddrAdapter & ia) override;
        void generate_patterns() override;

        Metrics &     get_metrics(mem_access_type) override;
        InstrInfo &   get_iinfo(mem_access_type) override;

        Metrics &     get_gather_metrics() override        { return _metrics.first;  }
        Metrics &     get_scatter_metrics() override       { return _metrics.second; }
        InstrInfo &   get_gather_iinfo () override         { return _iinfo.first;    }
        InstrInfo &   get_scatter_iinfo () override        { return _iinfo.second;   }
        TraceInfo &   get_trace_info() override            { return _trace_info;     }
        InstrWindow<MEMORY_ACCESS_SIZE> &
                      get_instr_window() override          { return _iw;             }

        void          set_log_level(int8_t level) override { _log_level = level;     }
        int8_t        get_log_level() override             { return _log_level;      }

        void set_trace_file(const std::string & trace_file_name) { _trace_file_name = trace_file_name; }
        const std::string & get_trace_file_name()          { return _trace_file_name; }

        void set_binary_file(const std::string & binary_file_name) { _binary_file_name = binary_file_name; }
        const std::string & get_binary_file_name()         { return _binary_file_name; }

        void update_metrics();

        std::string get_file_prefix ();

        void process_traces();
        void update_source_lines();
        double update_source_lines_from_binary(mem_access_type);
        void process_second_pass(gzFile & fp_drtrace);

    private:
        std::pair<Metrics, Metrics>     _metrics;
        std::pair<InstrInfo, InstrInfo> _iinfo;
        TraceInfo                       _trace_info;
        InstrWindow<MEMORY_ACCESS_SIZE> _iw;

        int8_t                          _log_level         = 0;

        std::string                     _trace_file_name;
        std::string                     _binary_file_name;
    };

} // namespace gspin_patterns

} // namespace gs_patterns