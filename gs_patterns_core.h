
#pragma once

#include <stdio.h>
#include <assert.h> /// TODO: use cassert instead
#include <math.h>
#include <string>

#include "gs_patterns.h"

namespace gs_patterns
{
namespace gs_patterns_core
{
    void translate_iaddr(const std::string & binary, char * source_line, addr_t iaddr);

    template <typename std::size_t T>
    void handle_trace_entry(MemPatterns<T> & mp, const InstrAddrAdapter & ia)
    {
        int i, j, k, w = 0;
        int w_rw_idx;   // Index into instruction window first dimension (RW: 0=Gather(R) or 1=Scatter(W))
        int w_idx;
        int gs;

        auto & trace_info = mp.get_trace_info();
        auto & gather_iinfo = mp.get_gather_iinfo();
        auto & scatter_iinfo = mp.get_scatter_iinfo();
        auto & gather_metrics = mp.get_gather_metrics();
        auto & scatter_metrics = mp.get_scatter_metrics();
        auto & iw = mp.get_instr_window();

        if (!ia.is_valid()) {
            std::ostringstream os;
            os << "Invalid " << ia;
            throw GSDataError(os.str());
        }

        if (ia.is_other_instr())
        {
            /*****************************/
            /** INSTR                   **/
            /*****************************/

            iw.get_iaddr() = ia.get_iaddr(); // was get_address in orig code -> get_iaddr()

            //nops
            trace_info.opcodes++;
            trace_info.did_opcode = true;
        }
        else if (ia.is_mem_instr())
        {
            /***********************/
            /** MEM instruction   **/
            /***********************/

            if (CTA == ia.get_mem_instr_type() && ia.get_base_addr() == ia.get_address()) {
                iw.get_iaddr() = ia.get_iaddr();
                trace_info.opcodes++;
                trace_info.did_opcode = true;
            }

            w_rw_idx = ia.get_type();

            //printf("M DRTRACE -- iaddr: %016lx addr: %016lx cl_start: %d bytes: %d\n",
            //     iw.iaddr,  ia.get_address(), ia.get_address() % 64, ia.get_size());

            if ((++trace_info.mcnt % PERSAMPLE) == 0) {
#if SAMPLE
                break;
#endif
                printf(".");
                fflush(stdout);
            }

            //is iaddr in window
            w_idx = -1;
            for (i = 0; i < IWINDOW; i++) {

                //new iaddr
                if (iw.w_iaddrs(w_rw_idx, i) == -1) {
                    w_idx = i;
                    break;

                    //iaddr exists
                } else if (iw.w_iaddrs(w_rw_idx, i) == iw.get_iaddr()) {
                    w_idx = i;
                    break;
                }
            }

            //new window
            if ((w_idx == -1) || (iw.w_bytes(w_rw_idx, w_idx) >= ia.get_max_access_size()) ||   // was >= VBYTES
                (iw.w_cnt(w_rw_idx, w_idx) >= ia.get_max_access_size())) {                      // was >= VBYTES

                /***************************/
                // do analysis
                /***************************/
                // i = each window
                for (w = 0; w < 2; w++) {  // 2

                    for (i = 0; i < IWINDOW; i++) {  // 1024

                        if (iw.w_iaddrs(w,i) == -1)
                            break;

                        int byte = iw.w_bytes(w, i) / iw.w_cnt(w, i);

                        // First pass - Determine gather/scatter?
                        gs = -1;
                        for (j = 0; j < iw.w_cnt(w, i); j++) {

                            // address and cl
                            iw.get_maddr() = iw.w_maddr(w, i, j);
                            assert(iw.get_maddr() > -1);

                            // previous addr
                            if (j == 0)
                                iw.get_maddr_prev() = iw.get_maddr() - 1;

                            // gather / scatter
                            if (iw.get_maddr() != iw.get_maddr_prev()) {
                                if ((gs == -1) && (abs(iw.get_maddr() - iw.get_maddr_prev()) > 1))  // ? > 1 stride (non-contiguous)   <--------------------
                                    gs = w;
                            }
                            iw.get_maddr_prev() = iw.get_maddr();
                        }

                        // Update other_cnt
                        if (gs == -1) trace_info.other_cnt += iw.w_cnt(w, i);

                        // GATHER or SCATTER handling
                        if (gs == 0 || gs == 1) {
                            InstrInfo & target_iinfo = (gs == 0) ? gather_iinfo : scatter_iinfo;

                            if (gs == 0) {
                                trace_info.gather_occ_avg += iw.w_cnt(w, i);
                                gather_metrics.cnt += 1.0;
                            }
                            else {
                                trace_info.scatter_occ_avg += iw.w_cnt(w, i);
                                scatter_metrics.cnt += 1.0;
                            }

                            for (k = 0; k < NGS; k++) {
                                if (target_iinfo.get_iaddrs()[k] == 0) {
                                    target_iinfo.get_iaddrs()[k] = iw.w_iaddrs(w, i);
                                    (target_iinfo.get_icnt()[k])++;
                                    target_iinfo.get_occ()[k] += iw.w_cnt(w, i);
                                    break;
                                }

                                if (target_iinfo.get_iaddrs()[k] == iw.w_iaddrs(w, i)) {
                                    (target_iinfo.get_icnt()[k])++;
                                    target_iinfo.get_occ()[k] += iw.w_cnt(w, i);
                                    break;
                                }
                            }
                        } // - if
                    } //WINDOW i - for

                    w_idx = 0;

                    // reset windows
                    iw.reset(w);
                } // rw w - for
            } // analysis - if

            // Set window values
            iw.w_iaddrs(w_rw_idx, w_idx) = iw.get_iaddr();
            iw.w_maddr(w_rw_idx, w_idx, iw.w_cnt(w_rw_idx, w_idx)) = ia.get_maddr();
            iw.w_bytes(w_rw_idx, w_idx) += ia.get_size();

            // num access per iaddr in loop
            iw.w_cnt(w_rw_idx, w_idx)++;

            if (trace_info.did_opcode) {

                trace_info.opcodes_mem++;
                trace_info.addrs++;
                trace_info.did_opcode = false;

            } else {
                trace_info.addrs++;
            }
        }
        else
        {
            /***********************/
            /** SOMETHING ELSE **/
            /***********************/

            trace_info.other++;
        }

        trace_info.trace_lines++;
    }

    template <typename std::size_t T>
    void display_stats(MemPatterns<T> & mp)
    {
        printf("\n RESULTS \n");

        printf("DRTRACE STATS\n");
        printf("DRTRACE LINES:        %16lu\n", mp.get_trace_info().trace_lines);
        printf("OPCODES:              %16lu\n", mp.get_trace_info().opcodes);
        printf("MEMOPCODES:           %16lu\n", mp.get_trace_info().opcodes_mem);
        printf("LOAD/STORES:          %16lu\n", mp.get_trace_info().addrs);
        printf("OTHER:                %16lu\n", mp.get_trace_info().other);

        printf("\n");

        printf("GATHER/SCATTER STATS: \n");
        printf("LOADS per GATHER:     %16.3f\n", mp.get_trace_info().gather_occ_avg);
        printf("STORES per SCATTER:   %16.3f\n", mp.get_trace_info().scatter_occ_avg);
        printf("GATHER COUNT:         %16.3f (log2)\n", log(mp.get_gather_metrics().cnt) / log(2.0));
        printf("SCATTER COUNT:        %16.3f (log2)\n", log(mp.get_scatter_metrics().cnt) / log(2.0));
        printf("OTHER  COUNT:         %16.3f (log2)\n", log(mp.get_trace_info().other_cnt) / log(2.0));
    }

    int get_top_target(InstrInfo & target_iinfo, Metrics & target_metrics);

    void normalize_stats(Metrics & target_metrics);

    bool handle_2nd_pass_trace_entry(const InstrAddrAdapter & ia,
                                     Metrics & gather_metrics, Metrics & scatter_metrics,
                                     addr_t & iaddr, int64_t & maddr, uint64_t & mcnt,
                                     addr_t * gather_base, addr_t * scatter_base);

    void create_metrics_file(FILE * fp,
                             FILE * fp2,
                             const std::string & file_prefix,
                             Metrics & target_metrics,
                             bool & first_spatter);

    template <typename std::size_t T>
    void create_spatter_file(MemPatterns<T> & mp, const std::string & file_prefix)
    {
        // Create spatter file
        FILE *fp, *fp2;

        if (file_prefix.empty()) throw GSFileError ("Empty file prefix provided.");

        std::string json_name = file_prefix + ".json";
        fp = fopen(json_name.c_str(), "w");
        if (NULL == fp) {
            throw GSFileError("Could not open " + json_name + "!");
        }

        std::string gs_info = file_prefix + ".txt";
        fp2 = fopen(gs_info.c_str(), "w");
        if (NULL == fp2) {
            throw GSFileError("Could not open " + gs_info + "!");
        }

        // Header
        fprintf(fp, "[ ");
        fprintf(fp2, "#sourceline, g/s, indices, percentage of g/s in trace\n");

        bool first_spatter = true;
        create_metrics_file(fp, fp2, file_prefix, mp.get_gather_metrics(), first_spatter);

        create_metrics_file(fp, fp2, file_prefix, mp.get_scatter_metrics(), first_spatter);

        // Footer
        fprintf(fp, " ]");
        fclose(fp);
        fclose(fp2);
    }

} // gs_patterns_core

} // gs_patterns
