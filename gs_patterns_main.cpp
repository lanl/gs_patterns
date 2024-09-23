#include <stdexcept>
#include <iostream>
#include <sstream>
#include <string>
#include <exception>

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
    std::cerr << "Usage: " << prog_name << " <pin_trace.gz> <prog_bin> \n"
              << "       " << prog_name << " <nvbit_trace.gz> -nv [-ow] [-v]" << std::endl;
}

int main(int argc, char ** argv)
{
    try
    {
        bool use_gs_nv = false;
        bool verbose = false;
        bool one_warp = false;
        for (int i = 0; i < argc; i++) {
            if (std::string(argv[i]) == "-nv") {
                use_gs_nv = true;
            }
            else if (std::string(argv[i]) == "-v") {
                verbose = true;
            }
            else if (std::string(argv[i]) == "-ow") {
                one_warp = true;
            }
        }

        size_t pos = std::string(argv[0]).find_last_of("/");
        std::string prog_name = std::string(argv[0]).substr(pos+1);

        if (argc < 3) {
            usage(prog_name);
            throw GSError("Invalid program arguments");
        }

        if (use_gs_nv)
        {
            MemPatternsForNV mp;

            mp.set_trace_file(argv[1]);

            const char * config_file = std::getenv(GSNV_CONFIG_FILE);
            if (config_file) {
                mp.set_config_file(config_file);
            }
            if (verbose) mp.set_log_level(1);
            if (one_warp) mp.set_one_warp_mode(one_warp);

            // ----------------- Process Traces -----------------

            mp.process_traces();

            // ----------------- Generate Patterns -----------------

            mp.generate_patterns();
        }
        else
        {
            MemPatternsForPin mp;

            mp.set_trace_file(argv[1]);
            mp.set_binary_file(argv[2]);
            if (verbose) mp.set_log_level(1);

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
