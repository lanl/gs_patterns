#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>
#include <unordered_map>
#include <set>
#include <filesystem>

#include <stdlib.h>
#include <stdio.h>
#include <cmath>
#include <string.h>
#include <algorithm>

#include "gs_patterns.h"
#include "gs_patterns_core.h"
#include "utils.h"

// Enable to use a vector for storing trace data for use by second pass (if not defined data is stored to a temp file
//#define USE_VECTOR_FOR_SECOND_PASS 1

#include "nvbit_tracing/gsnv_trace/common.h"

namespace gs_patterns
{
namespace gsnv_patterns
{
    constexpr std::size_t MEMORY_ACCESS_SIZE = 2048 / 8;

    struct _trace_entry_t {
        unsigned short type; // 2 bytes: trace_type_t
        unsigned short size;
        union {
            addr_t addr;
            unsigned char length[sizeof(addr_t)];
        };
        addr_t         base_addr;
        addr_t         iaddr;
        char           padding[4];
    }  __attribute__((packed));
    typedef struct _trace_entry_t trace_entry_t;

    #define MAP_NAME_SIZE 24
    #define MAP_VALUE_SIZE 22
    #define MAP_VALUE_LONG_SIZE 94
    #define NUM_MAPS 3
    // Setting this to fit within a 4k page e.g. 128 * 32 bytes <= 4k
    #define TRACE_BUFFER_LENGTH 128

    struct _trace_map_entry_t
    {
        // 32 bytes total
        char     map_name[MAP_NAME_SIZE];
        uint16_t id;
        char     val[MAP_VALUE_LONG_SIZE];
    };
    typedef struct _trace_map_entry_t trace_map_entry_t;

    struct _trace_header_t {
        uint64_t  num_maps;
        uint64_t  num_map_entires;
        uint64_t  total_traces;
    };
    typedef struct _trace_header_t trace_header_t;


    // An adapter for trace_entry_t (temporaritly untl replaced with nvbit memory detail type)
    class InstrAddrAdapterForNV : public InstrAddrAdapter
    {
    public:
        InstrAddrAdapterForNV(const trace_entry_t & te) : _te(te) { }

        virtual ~InstrAddrAdapterForNV() { }

        virtual inline bool            is_valid() const override            { return true;          }
        virtual inline bool            is_mem_instr() const override        { return true;          }
        virtual inline bool            is_other_instr() const override      { return false;         }
        virtual inline mem_access_type get_mem_access_type() const override { return (_te.type == 0) ? GATHER : SCATTER; }
        virtual inline mem_instr_type  get_mem_instr_type() const override  { return CTA;           }

        virtual inline size_t          get_size() const override            { return _te.size;      } // in bytes
        virtual inline addr_t          get_base_addr() const override       { return _te.base_addr; }
        virtual inline addr_t          get_address() const override         { return _te.addr;      }
        virtual inline addr_t          get_iaddr () const override          { return _te.iaddr;     }
        virtual inline addr_t          get_maddr () const override          { return _te.addr;      } // was _base_addr
        virtual inline unsigned short  get_type() const override            { return _te.type;      } // must be 0 for GATHER, 1 for SCATTER !!
        virtual inline int64_t         get_max_access_size() const override { return  MEMORY_ACCESS_SIZE; } // 32 * 8 bytes

        virtual void output(std::ostream & os) const override   {  os << "InstrAddrAdapterForNV: trace entry: type: ["
                                                                      << _te.type << "] size: [" << _te.size << "]";  }

        const trace_entry_t & get_trace_entry() const                       { return _te; }

    private:
        const trace_entry_t  _te;
    };

    class MemPatternsForNV : public MemPatterns<MEMORY_ACCESS_SIZE>
    {
    public:
        static const uint8_t CTA_LENGTH = 32;

        static constexpr const char * ID_TO_OPCODE         = "ID_TO_OPCODE";
        static constexpr const char * ID_TO_OPCODE_SHORT   = "ID_TO_OPCODE_SHORT";
        static constexpr const char * ID_TO_LINE           = "ID_TO_LINE";

        static constexpr const char * GSNV_TARGET_KERNEL   = "GSNV_TARGET_KERNEL";
        static constexpr const char * GSNV_TRACE_OUT_FILE  = "GSNV_TRACE_OUT_FILE";
        static constexpr const char * GSNV_PROGRAM_BINARY  = "GSNV_PROGRAM_BINARY";
        static constexpr const char * GSNV_FILE_PREFIX     = "GSNV_FILE_PREFIX";
        static constexpr const char * GSNV_MAX_TRACE_COUNT = "GSNV_MAX_TRACE_COUNT";
        static constexpr const char * GSNV_LOG_LEVEL       = "GSNV_LOG_LEVEL";
        static constexpr const char * GSNV_ONE_WARP_MODE   = "GSNV_ONE_WARP_MODE";


        MemPatternsForNV(): _metrics(GATHER, SCATTER),
                            _iinfo(GATHER, SCATTER),
                            _target_opcodes { "LD", "ST", "LDS", "STS", "LDG", "STG" }
        { }

        virtual ~MemPatternsForNV() override {  }

        void handle_trace_entry(const InstrAddrAdapter & ia) override;
        void generate_patterns() override;

        Metrics &     get_metrics(mem_access_type) override;
        InstrInfo &   get_iinfo(mem_access_type) override;

        Metrics &     get_gather_metrics() override  { return _metrics.first;  }
        Metrics &     get_scatter_metrics() override { return _metrics.second; }
        InstrInfo &   get_gather_iinfo () override   { return _iinfo.first;    }
        InstrInfo &   get_scatter_iinfo () override  { return _iinfo.second;   }
        TraceInfo &   get_trace_info() override      { return _trace_info;     }

        InstrWindow<MEMORY_ACCESS_SIZE> &
                get_instr_window() override          { return _iw;             }

        void          set_log_level(int8_t level) override      { _log_level = level;      }
        int8_t        get_log_level() override                  { return _log_level;       }

        void set_trace_file(const std::string & trace_file_name);
        inline const std::string & get_trace_file_name()        { return _trace_file_name; }

        inline void set_file_prefix(const std::string & prefix) { _file_prefix = prefix;   }
        std::string get_file_prefix();

        void set_one_warp_mode(bool val)                        { _one_warp_mode = val;    }

        void set_max_trace_count(int64_t max_trace_count);
        inline bool exceed_max_count() const {
            if (_limit_trace_count && (_trace_info.trace_lines >= _max_trace_count)) {
                return true;
            }
            return false;
        }

        // Mainly Called by nvbit kernel
        void set_config_file (const std::string & config_file);

        void update_metrics();

        void process_traces();
        void update_source_lines();
        double update_source_lines_from_binary(mem_access_type);
        void process_second_pass();

        std::string addr_to_line(addr_t addr)
        {
            auto itr = _addr_to_line_id.find(addr);
            if (itr != _addr_to_line_id.end()) {
                auto it2 = _id_to_line_map.find(itr->second);
                if (it2 != _id_to_line_map.end()) {
                    return it2->second;
                }
            }
            return std::string();
        }

        void set_trace_out_file(const std::string & trace_file_name);
        void write_trace_out_file();

        // Handle an nvbit CTA memory update
        void handle_cta_memory_access(const mem_access_t * ma);
        // Validate cta stride is within minimum
        bool valid_gs_stride(const std::vector<trace_entry_t> & te_list, const uint32_t min_stride);

        // TODO: Migrate these to template functions !
        // -----------------------------------------------------------------

        // Store opcode mappings
        bool add_or_update_opcode(int opcode_id, const std::string & opcode);
        // Retrieve opcode mapping by opcode_id
        const std::string & get_opcode(int opcode_id);

        // Store opcode_short mappings
        bool add_or_update_opcode_short(int opcode_short_id, const std::string & opcode_short);
        // Retrieve opcode_short mapping by opcode_short_id
        const std::string & get_opcode_short(int opcode_short_id);

        // Store line mappings
        bool add_or_update_line(int line_id, const std::string & line);
        // Retrieve line number mapping by line_id
        const std::string & get_line(int line_id);

        // -----------------------------------------------------------------

        bool should_instrument(const std::string & kernel_name);

        bool convert_to_trace_entry(const mem_access_t & ma, bool ignore_partial_warps, std::vector<trace_entry_t> & te_list);

    private:

        std::pair<Metrics, Metrics>        _metrics;
        std::pair<InstrInfo, InstrInfo>    _iinfo;
        TraceInfo                          _trace_info;
        InstrWindow<MEMORY_ACCESS_SIZE>    _iw;

        std::string                        _trace_file_name;            // Input compressed nvbit trace file
        std::string                        _file_prefix;                // Used by gs_patterns_core to write out pattern files
        std::string                        _trace_out_file_name;        // Ouput file containing nvbit traces encounterd if requested
        std::string                        _tmp_trace_out_file_name;    // Temp file used to store traces before re-writing to _trace_out_filename

        std::string                        _config_file_name;
        std::set<std::string>              _target_kernels;
        bool                               _limit_trace_count = false;
        int64_t                            _max_trace_count   = 0;
        uint64_t                           _traces_written    = 0;
        uint64_t                           _traces_handled    = 0;

        bool                               _write_trace_file  = false;
        bool                               _first_trace_seen  = false;

        int8_t                             _log_level         = 0;
        bool                               _one_warp_mode     = false;

        /* The output stream used to temporarily hold raw trace warp data (mem_access_t) before being writen to _trace_out_file_name */
        std::fstream                       _ofs_tmp;
        /* The output stream cooresponding to _trace_out_file_name. Used to store final nvbit trace data with header */
        std::ofstream                      _ofs;

    #ifdef USE_VECTOR_FOR_SECOND_PASS
        /* A vector used to store intermediate trace records (trace_entry_t) exclusively for use by second pass
           (instead of _tmp_dump_file if USE_VECTOR_FOR_SECOND_PASS is defined) */
        std::vector<InstrAddrAdapterForNV> _traces;
    #else
        /* A temp file used to store intermediate trace records (trace_entry_t) exclusively for use by second pass */
        std::FILE *                        _tmp_dump_file;
    #endif

        std::map<int, std::string>       _id_to_opcode_map;
        std::map<int, std::string>       _id_to_opcode_short_map;
        std::map<int, std::string>       _id_to_line_map;         // Contains source line_id to source line mappings
        std::unordered_map<addr_t, int>  _addr_to_line_id;        // Contains address to line_id mappings
        const std::set<std::string>      _target_opcodes;
    };

} // namespace gsnv_patterns

} // namespace gs_patterns
