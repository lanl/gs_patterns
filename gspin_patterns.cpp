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
#include "gspin_patterns.h"
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

namespace gs_patterns
{
namespace gspin_patterns
{

using namespace gs_patterns::gs_patterns_core;

int drline_read(gzFile fp, trace_entry_t * val, trace_entry_t ** p_val, int * edx)
{

    int idx;

    idx = (*edx) / sizeof(trace_entry_t);
    //first read
    if (NULL == *p_val) {
        *edx = gzread(fp, val, sizeof(trace_entry_t) * NBUFS);
        *p_val = val;

    } else if (*p_val == &val[idx]) {
        *edx = gzread(fp, val, sizeof(trace_entry_t) * NBUFS);
        *p_val = val;
    }

    if (0 == *edx)
        return 0;

    return 1;
}

Metrics & MemPatternsForPin::get_metrics(mem_access_type m)
{
    switch (m)
    {
        case GATHER : return _metrics.first;
        case SCATTER : return _metrics.second;
        default:
            throw GSError("Unable to get Metrics - Invalid Metrics Type: " + std::to_string(m));
    }
}

InstrInfo & MemPatternsForPin::get_iinfo(mem_access_type m)
{
    switch (m)
    {
        case GATHER : return _iinfo.first;
        case SCATTER : return _iinfo.second;
        default:
            throw GSError("Unable to get InstrInfo - Invalid Metrics Type: " + std::to_string(m));
    }
}

void MemPatternsForPin::handle_trace_entry(const InstrAddrAdapter & ia)
{
    // Call libgs_patterns
    gs_patterns_core::handle_trace_entry(*this, ia);
}

void MemPatternsForPin::generate_patterns()
{
    // ----------------- Update Source Lines -----------------

    update_source_lines();

    // ----------------- Update Metrics -----------------

    update_metrics();

    // ----------------- Create Spatter File -----------------

    create_spatter_file<MEMORY_ACCESS_SIZE>(*this, get_file_prefix());

}

void MemPatternsForPin::update_metrics()
{
    gzFile fp_drtrace;
    try
    {
        fp_drtrace = open_trace_file(get_trace_file_name());
    }
    catch (const std::runtime_error & ex)
    {
        throw GSFileError(ex.what());
    }

    // Get top gathers
    get_gather_metrics().ntop = get_top_target(get_gather_iinfo(), get_gather_metrics());

    // Get top scatters
    get_scatter_metrics().ntop = get_top_target(get_scatter_iinfo(), get_scatter_metrics());

    // ----------------- Second Pass -----------------

    process_second_pass(fp_drtrace);

    // ----------------- Normalize -----------------

    normalize_stats(get_gather_metrics());
    normalize_stats(get_scatter_metrics());

    close_trace_file(fp_drtrace);
}

std::string MemPatternsForPin::get_file_prefix()
{
    std::string prefix = _trace_file_name;
    size_t pos = std::string::npos;
    while (std::string::npos != (pos = prefix.find(".gz")))
    {
        prefix.replace(pos, 3, "");
    }
    return prefix;
}

double MemPatternsForPin::update_source_lines_from_binary(mem_access_type mType)
{
    double target_cnt = 0.0;

    InstrInfo & target_iinfo   = get_iinfo(mType);
    Metrics &   target_metrics = get_metrics(mType);

    //Check it is not a library
    for (int k = 0; k < NGS; k++) {

        if (0 == target_iinfo.get_iaddrs()[k]) {
            break;
        }
        translate_iaddr(get_binary_file_name(), target_metrics.get_srcline()[k], target_iinfo.get_iaddrs()[k]);
        if (startswith(target_metrics.get_srcline()[k], "?"))
            target_iinfo.get_icnt()[k] = 0;

        target_cnt += target_iinfo.get_icnt()[k];
    }
    printf("done.\n");

    return target_cnt;
}

// First Pass
void MemPatternsForPin::process_traces()
{
    int iret = 0;
    trace_entry_t *drline;
    gzFile fp_drtrace;

    try
    {
        fp_drtrace = open_trace_file(get_trace_file_name());
    }
    catch (const std::runtime_error & ex)
    {
        throw GSFileError(ex.what());
    }

    printf("First pass to find top gather / scatter iaddresses\n");
    fflush(stdout);

    uint64_t lines_read = 0;
    trace_entry_t *p_drtrace = NULL;
    trace_entry_t drtrace[NBUFS];  // was static (1024 bytes)

    while (drline_read(fp_drtrace, drtrace, &p_drtrace, &iret)) {
        //decode drtrace
        drline = p_drtrace;

        handle_trace_entry(InstrAddrAdapterForPin(drline));

        p_drtrace++;
        lines_read++;
    }

    std::cout << "Lines Read: " << lines_read << std::endl;

    close_trace_file(fp_drtrace);

    //metrics
    get_trace_info().gather_occ_avg /= get_gather_metrics().cnt;
    get_trace_info().scatter_occ_avg /= get_scatter_metrics().cnt;

    display_stats<MEMORY_ACCESS_SIZE>(*this);

}

void MemPatternsForPin::process_second_pass(gzFile & fp_drtrace)
{
    uint64_t mcnt = 0;  // used our own local mcnt while iterating over file in this method.
    int iret = 0;
    trace_entry_t *drline;

    // State carried thru
    addr_t iaddr;
    int64_t maddr;
    addr_t gather_base[NTOP] = {0};
    addr_t scatter_base[NTOP] = {0};

    bool breakout = false;
    printf("\nSecond pass to fill gather / scatter subtraces\n");
    fflush(stdout);

    trace_entry_t *p_drtrace = NULL;
    trace_entry_t drtrace[NBUFS];   // was static (1024 bytes)

    while (drline_read(fp_drtrace, drtrace, &p_drtrace, &iret) && !breakout) {
        //decode drtrace
        drline = p_drtrace;

        breakout = handle_2nd_pass_trace_entry(InstrAddrAdapterForPin(drline), get_gather_metrics(), get_scatter_metrics(),
                                                 iaddr, maddr, mcnt, gather_base, scatter_base);

        p_drtrace++;
    }
}

void MemPatternsForPin::update_source_lines()
{
    // Find source lines for gathers - Must have symbol
    printf("\nSymbol table lookup for gathers...");
    fflush(stdout);

    get_gather_metrics().cnt = update_source_lines_from_binary(GATHER);

    // Find source lines for scatters
    printf("Symbol table lookup for scatters...");
    fflush(stdout);

    get_scatter_metrics().cnt = update_source_lines_from_binary(SCATTER);
}

} // namespace gspin_patterns

} // namespace gs_patterns