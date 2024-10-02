#include <stdexcept>
#include <iostream>
#include <sstream>
#include <string>
#include <exception>
#include <getopt.h>

#include "gs_patterns.h"
#include "gs_patterns_core.h"
#include "gspin_patterns.h"
#include "gsnv_patterns.h"
#include "utils.h"

#define GSNV_CONFIG_FILE "GSNV_CONFIG_FILE"

using namespace gs_patterns;
using namespace gs_patterns::gs_patterns_core;
using namespace gs_patterns::gsnv_patterns;
using namespace gs_patterns::gspin_patterns;

void usage (const std::string & prog_name)
{
    std::cerr << "Usage: " << prog_name << " <pin_trace.gz> <prog_bin> <options>       \n"
              << "       " << prog_name << " <nvbit_trace.gz> <options>   \n"
              << "              [ -n, -nvbit ]                             - Trace file provided is NVBit trace\n"
              << "              [ -w, -num_strides <number strides> ]      - Use memory acceses from one warp only (warp 0)\n"
              << "              [ -v, -verbose ]                           - Verbose output\n"
              << "Additional options: \n"
              << "              [ -a, -num_accesses <number of accesses> ] - Threshold for number of accesses\n"
              << "              [ -s, -num_strides <number strides> ]      - Threshold for number of unique distances\n"
              << "              [ -o, -out_dist_percent <out threshold> ]     - Threshold for percentage of distances at boundaries of histogram\n"
              << "              [ -help, -h ]                              - Dispaly program options\n"
              << std::endl;
}

bool get_arg_int(const char * arg, int & value)
{
    try {
        value = stoi(std::string(arg));
        return true;
    }
    catch (...) {
        std::cerr << "ERROR: Unable to convert [" + std::string(arg) + "] to integer" << std::endl;
    }
    return false;
}

bool get_arg_float(const char * arg, float & value)
{
    try {
        value = stof(std::string(arg));
        return true;
    }
    catch (...) {
        std::cerr << "ERROR: Unable to convert [" + std::string(arg) + "] to float" << std::endl;
    }
    return false;
}

struct ProgramArgs
{
    std::string prog_name;
    bool        use_gs_nv        = false;
    bool        verbose          = false;
    bool        one_warp         = false;
    int         num_accesses     = -1;
    int         num_strides      = -1;
    float       out_dist_percent = -1.0;
    std::string trace_file_name;
    std::string binary_file_name;

    void get_args(char** argv, int &argc)
    {
        static struct option options[] = {
            {"nvbit",           no_argument, 0, 'n'},
            {"one_warp",        no_argument, 0, 'w'},
            {"verbose",         no_argument, 0, 'v'},
            {"help",            no_argument, 0, 'h'},
            {"num_accesses" ,   required_argument, 0, 'a'},
            {"num_strides",     required_argument, 0, 's'},
            {"out_dist_percent",   required_argument, 0, 'o'},
            {0,                 0,           0, 0  }
        };
        int option_index = 0;

        while (true)
        {
            int c = getopt_long(argc, argv, "nwvha:s:o:", options, &option_index);

            if (c == -1) break;

            int n = 0;
            switch (c) {
                case 'n':
                    use_gs_nv = true;
                    break;

                case 'w':
                    one_warp = true;
                    break;

                case 'v':
                    verbose = true;
                    break;

                case 'a':
                    if (!get_arg_int(optarg, num_accesses)) {
                        usage(prog_name);
                        exit(-1);
                    }
                    break;

                case 's':
                    if (!get_arg_int(optarg, num_strides)) {
                        usage(prog_name);
                        exit(-1);
                    }
                    break;

                case 'o':
                    if (!get_arg_float(optarg, out_dist_percent)) {
                        usage(prog_name);
                        exit(-1);
                    }
                    break;

                case 'h':
                    usage(prog_name);
                    exit(0);

                default:
                    usage(prog_name);
                    exit(-1);
            }
        }

        // Handle Positional args
        if (optind < argc) {
            trace_file_name = argv[optind++]; // was 1
        }
        if (optind < argc) {
            binary_file_name = argv[optind++]; // was 2
        }
    }
};

void updateThresholds(Thresholds & thresholds, ProgramArgs & pArgs)
{
    if (pArgs.num_strides >= 0)
        thresholds.num_strides = pArgs.num_strides;
    if (pArgs.num_accesses >= 0)
        thresholds.num_accesses = pArgs.num_accesses;
    if (pArgs.out_dist_percent >= 0.0)
        thresholds.out_dist_percent = pArgs.out_dist_percent;
}

int main(int argc, char ** argv)
{
    try
    {
        size_t pos = std::string(argv[0]).find_last_of("/");
        std::string prog_name = std::string(argv[0]).substr(pos+1);

        ProgramArgs pArgs;
        pArgs.get_args(argv, argc);

        if (argc < 3) {
            usage(prog_name);
            throw GSError("Invalid program arguments");
        }

        if (pArgs.use_gs_nv)
        {
            MemPatternsForNV mp;

            mp.set_trace_file(pArgs.trace_file_name);

            const char * config_file = std::getenv(GSNV_CONFIG_FILE);
            if (config_file) {
                mp.set_config_file(config_file);
            }
            if (pArgs.verbose) mp.set_log_level(1);
            if (pArgs.one_warp) mp.set_one_warp_mode(pArgs.one_warp);

            updateThresholds(mp.get_thresholds(), pArgs);

            // ----------------- Process Traces -----------------

            mp.process_traces();

            // ----------------- Generate Patterns -----------------

            mp.generate_patterns();
        }
        else
        {
            MemPatternsForPin mp;

            mp.set_trace_file(pArgs.trace_file_name);
            mp.set_binary_file(pArgs.binary_file_name);
            if (pArgs.verbose) mp.set_log_level(1);

            updateThresholds(mp.get_thresholds(), pArgs);

            // ----------------- Process Traces -----------------

            mp.process_traces();

            // ----------------- Generate Patterns -----------------

            mp.generate_patterns();
        }
    }
    catch (const GSFileError & ex)
    {
        std::cerr << "ERROR: <GSFileError> " << ex.what() << std::endl;
        exit(-1);
    }
    catch (const GSAllocError & ex)
    {
        std::cerr << "ERROR: <GSAllocError> " << ex.what() << std::endl;
        exit(-1);
    }
    catch (const GSDataError & ex)
    {
        std::cerr << "ERROR: <GSDataError> " << ex.what() << std::endl;
        exit(1);
    }
    catch (const GSError & ex)
    {
        std::cerr << "ERROR: <GSError> " << ex.what() << std::endl;
        exit(1);
    }
    catch (const std::exception & ex)
    {
        std::cerr << "ERROR: " << ex.what() << std::endl;
        exit(-1);
    }

    return 0;
}
