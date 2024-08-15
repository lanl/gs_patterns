
#include <assert.h> /// TODO: use cassert instead
#include <math.h>

#include <string>
#include <sstream>

#include "utils.h"
#include "gs_patterns.h"

namespace gs_patterns
{
namespace gs_patterns_core
{
    using namespace gs_patterns;

    void translate_iaddr(const std::string & binary, char * source_line, addr_t iaddr)
    {
        char path[MAX_LINE_LENGTH];
        char cmd[MAX_LINE_LENGTH];
        FILE *fp;

        sprintf(cmd, "addr2line -e %s 0x%lx", binary.c_str(), iaddr);

        /* Open the command for reading. */
        fp = popen(cmd, "r");
        if (NULL == fp) {
            throw GSError("Failed to run command");
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

    void create_metrics_file(FILE * fp, FILE * fp2, const std::string & file_prefix, Metrics & target_metrics, bool & first_spatter)
    {
        int i = 0;
        int j = 0;

        //Create stride histogram and create spatter
        int sidx;
        int unique_strides;
        int64_t n_stride[1027];
//      double outbounds;

        if (file_prefix.empty()) throw GSFileError ("Empty file prefix provided.");

        if (first_spatter) printf("\n");

        printf("\n");
        for (i = 0; i < target_metrics.ntop; i++) {
            printf("***************************************************************************************\n");

            unique_strides = 0;
            for (j = 0; j < 1027; j++)
                n_stride[j] = 0;

            for (j = 1; j < target_metrics.offset[i]; j++) {
                sidx = target_metrics.patterns[i][j] - target_metrics.patterns[i][j - 1] + 513;
                sidx = (sidx < 1) ? 0 : sidx;
                sidx = (sidx > 1025) ? 1026 : sidx;
                n_stride[sidx]++;
            }

            for (j = 0; j < 1027; j++) {
                if (n_stride[j] > 0) {
                    unique_strides++;
                }
            }

            //outbounds = (double) (n_stride[0] + n_stride[1026]) / (double) target_metrics.offset[i];

            //if ( ( (unique_strides > NSTRIDES) || (outbounds > OUTTHRESH) ) && (gather_offset[i] > USTRIDES ) ){
            if (true) {

                //create a binary file
                FILE *fp_bin;
                std::string bin_name = file_prefix + ".sbin";
                printf("%s\n", bin_name.c_str());
                fp_bin = fopen(bin_name.c_str(), "w");
                if (NULL == fp_bin) {
                    throw GSFileError("Could not open " + std::string(bin_name) + "!");
                }

                printf("%sIADDR    -- %p\n", target_metrics.getShortName().c_str(), (void*) target_metrics.top[i]);
                printf("SRCLINE   -- %s\n", target_metrics.get_srcline()[target_metrics.top_idx[i]]);
                printf("%s %c -- %6.3f%c (512-bit chunks)\n", target_metrics.type_as_string().c_str(),
                       '%', 100.0 * (double) target_metrics.tot[i] / target_metrics.cnt, '%');
                printf("NDISTS  -- %ld\n", (long int)target_metrics.offset[i]);

                int64_t nlcnt = 0;
                for (j = 0; j < target_metrics.offset[i]; j++) {

                    if (j < 39) {
                        printf("%10ld ", target_metrics.patterns[i][j]);
                        fflush(stdout);
                        if (0 == (++nlcnt % 13))
                            printf("\n");

                    } else if (j >= (target_metrics.offset[i] - 39)) {
                        printf("%10ld ", target_metrics.patterns[i][j]);
                        fflush(stdout);
                        if (0 == (++nlcnt % 13))
                            printf("\n");

                    } else if (39 == j)
                        printf("...\n");
                }
                printf("\n");
                printf("DIST HISTOGRAM --\n");

                for (j = 0; j < 1027; j++) {
                    if (n_stride[j] > 0) {
                        if (0 == j)
                            printf("%6s: %ld\n", "< -512", n_stride[j]);
                        else if (1026 == j)
                            printf("%6s: %ld\n", ">  512", n_stride[j]);
                        else
                            printf("%6d: %ld\n", j - 513, n_stride[j]);
                    }
                }

                if (first_spatter) {
                    first_spatter = false;
                    fprintf(fp, " {\"kernel\":\"%s\", \"pattern\":[", target_metrics.getName().c_str());
                } else {
                    fprintf(fp, ",\n {\"kernel\":\"%s\", \"pattern\":[", target_metrics.getName().c_str());
                }

                fwrite(target_metrics.patterns[i], sizeof(uint64_t), target_metrics.offset[i], fp_bin);
                fclose(fp_bin);

                for (j = 0; j < target_metrics.offset[i] - 1; j++)
                    fprintf(fp, "%ld,", target_metrics.patterns[i][j]);
                fprintf(fp, "%ld", target_metrics.patterns[i][target_metrics.offset[i] - 1]);
                fprintf(fp, "], \"count\":1}");

                fprintf(fp2, "%s,%s,%ld,%6.3f\n",
                        target_metrics.get_srcline()[target_metrics.top_idx[i]], target_metrics.getShortName().c_str(),
                        (long int)target_metrics.offset[i],
                        100.0 * (double) target_metrics.tot[i] / target_metrics.cnt);
            }
            printf("***************************************************************************************\n\n");
        }
    }

    void normalize_stats(Metrics & target_metrics)
    {
        //Normalize
        int64_t smallest;
        for (int i = 0; i < target_metrics.ntop; i++) {

            //Find smallest
            smallest = 0;
            for (int j = 0; j < target_metrics.offset[i]; j++) {
                if (target_metrics.patterns[i][j] < smallest)
                    smallest = target_metrics.patterns[i][j];
            }

            smallest *= -1;

            //Normalize
            for (int j = 0; j < target_metrics.offset[i]; j++) {
                target_metrics.patterns[i][j] += smallest;
            }
        }
    }

    int get_top_target(InstrInfo & target_iinfo, Metrics & target_metrics)
    {
        int target_ntop = 0;

        for (int j = 0; j < NTOP; j++)
        {
            int bestcnt = 0;
            addr_t best_iaddr = 0;
            int bestidx = -1;

            for (int k = 0; k < NGS; k++)
            {
                if (target_iinfo.get_icnt()[k] == 0)
                    continue;

                if (target_iinfo.get_iaddrs()[k] == 0) {
                    break;
                }

                if (target_iinfo.get_icnt()[k] > bestcnt) {
                    bestcnt = target_iinfo.get_icnt()[k];
                    best_iaddr = target_iinfo.get_iaddrs()[k];
                    bestidx = k;
                }
            }

            if (best_iaddr == 0)
            {
                break;
            }
            else
            {
                target_ntop++;
                target_metrics.top[j] = best_iaddr;
                target_metrics.top_idx[j] = bestidx;
                target_metrics.tot[j] = target_iinfo.get_icnt()[bestidx];
                target_iinfo.get_icnt()[bestidx] = 0;

                //printf("%sIADDR -- %016lx: %16lu -- %s\n", target_metrics.getShortName().c_str(), target_metrics.top[j], target_metrics.tot[j], target_metrics.get_srcline()[bestidx]);
            }
        } // for

        return target_ntop;
    }

    bool handle_2nd_pass_trace_entry(const InstrAddrAdapter & ia,
                                     Metrics & gather_metrics, Metrics & scatter_metrics,
                                     addr_t & iaddr, int64_t & maddr, uint64_t & mcnt,
                                     addr_t * gather_base, addr_t * scatter_base)
    {
        int iret = 0;
        int i = 0;

        bool breakout = false;

        /*****************************/
        /** INSTR 0xa-0x10 and 0x1e **/
        /*****************************/
        if (!ia.is_valid()) {
            std::ostringstream os;
            os << "Invalid " << ia;
            throw GSDataError(os.str());
        }

        if (ia.is_other_instr())
        {
            iaddr = ia.get_iaddr();  // was get_address in orig code  -> get_iaddr()
        }
        else if (ia.is_mem_instr())
        {
            /***********************/
            /** MEM               **/
            /***********************/

            maddr = ia.get_maddr();

            if (CTA == ia.get_mem_instr_type() && ia.get_address() == ia.get_base_addr()) {
                iaddr = ia.get_iaddr();
            }

            if ((++mcnt % PERSAMPLE) == 0) {
    #if SAMPLE
                break;
    #endif
                printf(".");
                fflush(stdout);
            }

            // gather ?
            if (GATHER == ia.get_mem_access_type())
            {
                for (i = 0; i < gather_metrics.ntop; i++)
                {
                    //found it
                    if (iaddr == gather_metrics.top[i])
                    {
                        if (gather_base[i] == 0)
                            gather_base[i] = maddr;

                        //Add index
                        if (gather_metrics.offset[i] >= gather_metrics.get_pattern_size(i)) {
                            if (!gather_metrics.grow(i)) {
                                printf("WARNING: Unable to increase PSIZE. Truncating trace...\n");
                                breakout = true;
                            }
                        }
                        gather_metrics.patterns[i][gather_metrics.offset[i]++] = (int64_t) (maddr - gather_base[i]);
                        break;
                    }
                }
            }
            // scatter ?
            else if (SCATTER == ia.get_mem_access_type())
            {
                for (i = 0; i < scatter_metrics.ntop; i++)
                {
                    //found it
                    if (iaddr == scatter_metrics.top[i])
                    {
                        //set base
                        if (scatter_base[i] == 0)
                            scatter_base[i] = maddr;

                        //Add index
                        if (scatter_metrics.offset[i] >= scatter_metrics.get_pattern_size(i)) {
                            if (!scatter_metrics.grow(i)) {
                                printf("WARNING: Unable to increase PSIZE. Truncating trace...\n");
                                breakout = true;
                            }
                        }
                        scatter_metrics.patterns[i][scatter_metrics.offset[i]++] = (int64_t) (maddr - scatter_base[i]);
                        break;
                    }
                }
            }
            else
            { // belt and suspenders, yep = but helps to validate correct logic in children of InstrAddresInfo
                throw GSDataError("Unknown Memory Access Type: " + std::to_string(ia.get_mem_access_type()));
            }
        } // MEM

        return breakout;
    }

} // namespace gs_patterns_core

std::ostream & operator<<(std::ostream & os, const gs_patterns::InstrAddrAdapter & ia)
{
    ia.output(os);
    return os;
}

} // namespace gs_patterns


