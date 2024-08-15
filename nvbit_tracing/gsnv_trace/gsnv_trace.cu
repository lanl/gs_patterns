/* Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <unordered_map>

/* every tool needs to include this once */
#include "nvbit_tool.h"

/* nvbit interface file */
#include "nvbit.h"

/* for channel */
#include "utils/channel.hpp"

/* contains definition of the mem_access_t structure */
#include "common.h"

#include <gs_patterns.h>
#include <gs_patterns_core.h>
#include <gsnv_patterns.h>

using namespace gs_patterns;
using namespace gs_patterns::gs_patterns_core;
using namespace gs_patterns::gsnv_patterns;

#define HEX(x)                                                            \
    "0x" << std::setfill('0') << std::setw(16) << std::hex << (uint64_t)x \
         << std::dec

#define CHANNEL_SIZE (1l << 20)

struct CTXstate {
    /* context id */
    int id;

    /* Channel used to communicate from GPU to CPU receiving thread */
    ChannelDev* channel_dev;
    ChannelHost channel_host;

    volatile bool recv_thread_done = false;
};

/* lock */
pthread_mutex_t mutex;
pthread_mutex_t cuda_event_mutex;

/* map to store context state */
std::unordered_map<CUcontext, CTXstate*> ctx_state_map;

/* skip flag used to avoid re-entry on the nvbit_callback when issuing
 * flush_channel kernel call */
bool skip_callback_flag = false;

/* global control variables for this tool */
uint32_t instr_begin_interval = 0;
uint32_t instr_end_interval = UINT32_MAX;
int verbose = 0;
std::string gsnv_config_file;

/* opcode to id map and reverse map  */
std::map<std::string, int> opcode_to_id_map;
std::map<int, std::string> id_to_opcode_map;
std::map<std::string, int> opcode_short_to_id_map;
std::map<std::string, int> line_to_id_map;

// Instantiate GSPatterns for NVBit
std::unique_ptr<MemPatternsForNV> mp(new MemPatternsForNV);


/* grid launch id, incremented at every launch */
uint64_t grid_launch_id = 0;

void* recv_thread_fun(void* args);

void nvbit_at_init() {
    setenv("CUDA_MANAGED_FORCE_DEVICE_ALLOC", "1", 1);
    GET_VAR_INT(
        instr_begin_interval, "INSTR_BEGIN", 0,
        "Beginning of the instruction interval where to apply instrumentation");
    GET_VAR_INT(
        instr_end_interval, "INSTR_END", UINT32_MAX,
        "End of the instruction interval where to apply instrumentation");
    GET_VAR_INT(verbose, "TOOL_VERBOSE", 0, "Enable verbosity inside the tool");

    GET_VAR_STR(gsnv_config_file, "GSNV_CONFIG_FILE", "Specify a GSNV config file");

    std::string pad(100, '-');
    printf("%s\n", pad.c_str());

    /* set mutex as recursive */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &attr);

    pthread_mutex_init(&cuda_event_mutex, &attr);
}

/* Set used to avoid re-instrumenting the same functions multiple times */
std::unordered_set<CUfunction> already_instrumented;

void instrument_function_if_needed(CUcontext ctx, CUfunction func) {
    assert(ctx_state_map.find(ctx) != ctx_state_map.end());
    CTXstate* ctx_state = ctx_state_map[ctx];

    /* Get related functions of the kernel (device function that can be
     * called by the kernel) */
    std::vector<CUfunction> related_functions =
        nvbit_get_related_functions(ctx, func);

    /* add kernel itself to the related function vector */
    related_functions.push_back(func);

    /* iterate on function */
    for (auto f : related_functions) {
        /* "recording" function was instrumented, if set insertion failed
         * we have already encountered this function */
        if (!already_instrumented.insert(f).second) {
            continue;
        }

        /* get vector of instructions of function "f" */
        const std::vector<Instr*>& instrs = nvbit_get_instrs(ctx, f);

        if (verbose) {
            printf(
                "GSNV_TRACE: CTX %p, Inspecting CUfunction %p name %s at address "
                "0x%lx\n",
                ctx, f, nvbit_get_func_name(ctx, f), nvbit_get_func_addr(f));
        }

        // Get address of function PC
        uint64_t func_addr = nvbit_get_func_addr(f);

        uint32_t cnt = 0;
        /* iterate on all the static instructions in the function */
        for (auto instr : instrs) {
            if (cnt < instr_begin_interval || cnt >= instr_end_interval ||
                instr->getMemorySpace() == InstrType::MemorySpace::NONE ||
                instr->getMemorySpace() == InstrType::MemorySpace::CONSTANT) {
                cnt++;
                continue;
            }
            if (verbose) {
                instr->printDecoded();
            }

            // Opcode to OpCodeID
            if (opcode_to_id_map.find(instr->getOpcode()) == opcode_to_id_map.end()) {
                int opcode_id = opcode_to_id_map.size();
                opcode_to_id_map[instr->getOpcode()] = opcode_id;
                id_to_opcode_map[opcode_id] = std::string(instr->getOpcode());
            }

            int opcode_id = opcode_to_id_map[instr->getOpcode()];

            // Opcode_Short to OpCode_Short_ID
            if (opcode_short_to_id_map.find(instr->getOpcodeShort()) == opcode_short_to_id_map.end()) {
                int opcode_short_id = opcode_short_to_id_map.size();
                opcode_short_to_id_map[instr->getOpcodeShort()] = opcode_short_id;
                //id_to_opcode_map[opcode_id] = std::string(instr->getOpcode());
            }
            int opcode_short_id = opcode_short_to_id_map[instr->getOpcodeShort()];

            // Line to Line_ID
            /* Get line information for a particular instruction offset if available, */
            /* binary must be compiled with --generate-line-info   (-lineinfo) */
            char * line_str;
            char * dir_str;
            uint32_t line_num;
            bool status = nvbit_get_line_info(ctx, func, instr->getOffset(), &line_str, &dir_str, &line_num);

            std::string line;
            int line_id = -1;
            if (status) {
                std::stringstream ss;
                ss << dir_str << line_str << ":" << line_num;
                line = ss.str();

                if (line_to_id_map.find(line) == line_to_id_map.end()) {
                    line_id = line_to_id_map.size();
                    line_to_id_map[line] = line_id;
                }
                line_id = line_to_id_map[line];
                //std::cout << "Creating a mapping from: " << line << " to line_id: " << line_id << std::endl;
            }

            // Let MemPatternsForNV know about the mapping
            mp->add_or_update_opcode(opcode_id, instr->getOpcode());
            mp->add_or_update_opcode_short(opcode_short_id, instr->getOpcodeShort());
            if (status) { mp->add_or_update_line(line_id, line); }

            // Compute instruction address (function address + instruction offset)
            uint64_t instr_addr = func_addr + instr->getOffset();

            int mref_idx = 0;
            /* iterate on the operands */
            for (int i = 0; i < instr->getNumOperands(); i++) {
                /* get the operand "i" */
                const InstrType::operand_t* op = instr->getOperand(i);

                if (op->type == InstrType::OperandType::MREF) {
                    /* insert call to the instrumentation function with its
                     * arguments */
                    nvbit_insert_call(instr, "instrument_mem", IPOINT_BEFORE);
                    /* predicate value */
                    nvbit_add_call_arg_guard_pred_val(instr);
                    /* opcode id */
                    nvbit_add_call_arg_const_val32(instr, opcode_id);

                    /* opcode short id */
                    nvbit_add_call_arg_const_val32(instr, opcode_short_id);
                    /* isLoad */
                    nvbit_add_call_arg_const_val32(instr, instr->isLoad());
                    /* isStore */
                    nvbit_add_call_arg_const_val32(instr, instr->isStore());
                    /* size */
                    nvbit_add_call_arg_const_val32(instr, instr->getSize());
                    /* line number id */
                    nvbit_add_call_arg_const_val32(instr, line_id);

                    /* Memory instruction address */
                    nvbit_add_call_arg_const_val64(instr, instr_addr);

                    /* memory reference 64 bit address */
                    nvbit_add_call_arg_mref_addr64(instr, mref_idx);
                    /* add "space" for kernel function pointer that will be set
                     * at launch time (64 bit value at offset 0 of the dynamic
                     * arguments)*/
                    nvbit_add_call_arg_launch_val64(instr, 0);
                    /* add pointer to channel_dev*/
                    nvbit_add_call_arg_const_val64(
                        instr, (uint64_t)ctx_state->channel_dev);
                    mref_idx++;
                }
            }
            cnt++;
        }
    }
}

/* flush channel */
__global__ void flush_channel(ChannelDev* ch_dev) { ch_dev->flush(); }

void init_context_state(CUcontext ctx) {
    CTXstate* ctx_state = ctx_state_map[ctx];
    ctx_state->recv_thread_done = false;
    cudaMallocManaged(&ctx_state->channel_dev, sizeof(ChannelDev));
    ctx_state->channel_host.init((int)ctx_state_map.size() - 1, CHANNEL_SIZE,
                                 ctx_state->channel_dev, recv_thread_fun, ctx);
    nvbit_set_tool_pthread(ctx_state->channel_host.get_thread());
}

void nvbit_at_cuda_event(CUcontext ctx, int is_exit, nvbit_api_cuda_t cbid,
                         const char* name, void* params, CUresult* pStatus) {
    pthread_mutex_lock(&cuda_event_mutex);

    /* we prevent re-entry on this callback when issuing CUDA functions inside
     * this function */
    if (skip_callback_flag) {
        pthread_mutex_unlock(&cuda_event_mutex);
        return;
    }
    skip_callback_flag = true;

    if (cbid == API_CUDA_cuLaunchKernel_ptsz ||
        cbid == API_CUDA_cuLaunchKernel ||
        cbid == API_CUDA_cuLaunchCooperativeKernel ||
        cbid == API_CUDA_cuLaunchCooperativeKernel_ptsz ||
        cbid == API_CUDA_cuLaunchKernelEx ||
        cbid == API_CUDA_cuLaunchKernelEx_ptsz) {
        CTXstate* ctx_state = ctx_state_map[ctx];

        CUfunction func;
        if (cbid == API_CUDA_cuLaunchKernelEx_ptsz ||
            cbid == API_CUDA_cuLaunchKernelEx) {
            cuLaunchKernelEx_params* p = (cuLaunchKernelEx_params*)params;
            func = p->f;
        } else {
            cuLaunchKernel_params* p = (cuLaunchKernel_params*)params;
            func = p->f;
        }

        if (!is_exit && mp->should_instrument(nvbit_get_func_name(ctx, func)))
        {
            /* Make sure GPU is idle */
            cudaDeviceSynchronize();
            assert(cudaGetLastError() == cudaSuccess);

            /* instrument */
            instrument_function_if_needed(ctx, func);

            int nregs = 0;
            CUDA_SAFECALL(
                cuFuncGetAttribute(&nregs, CU_FUNC_ATTRIBUTE_NUM_REGS, func));

            int shmem_static_nbytes = 0;
            CUDA_SAFECALL(
                cuFuncGetAttribute(&shmem_static_nbytes,
                                   CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, func));

            /* get function name and pc */
            const char* func_name = nvbit_get_func_name(ctx, func);
            uint64_t pc = nvbit_get_func_addr(func);

            /* set grid launch id at launch time */
            nvbit_set_at_launch(ctx, func, (uint64_t)&grid_launch_id);

            /* enable instrumented code to run */
            nvbit_enable_instrumented(ctx, func, true);

            if (cbid == API_CUDA_cuLaunchKernelEx_ptsz ||
                cbid == API_CUDA_cuLaunchKernelEx)
            {
                cuLaunchKernelEx_params *p = (cuLaunchKernelEx_params *) params;
                printf(
                    "GSNV_TRACE: CTX 0x%016lx - LAUNCH - Kernel pc 0x%016lx - "
                    "Kernel name %s - grid launch id %ld - grid size %d,%d,%d "
                    "- block size %d,%d,%d - nregs %d - shmem %d - cuda stream "
                    "id %ld\n",
                    (uint64_t)ctx, pc, func_name, grid_launch_id,
                    p->config->gridDimX, p->config->gridDimY,
                    p->config->gridDimZ, p->config->blockDimX,
                    p->config->blockDimY, p->config->blockDimZ, nregs,
                    shmem_static_nbytes + p->config->sharedMemBytes,
                    (uint64_t)p->config->hStream);
            }
            else
            {
                cuLaunchKernel_params* p = (cuLaunchKernel_params*)params;
                printf(
                    "GSNV_TRACE: CTX 0x%016lx - LAUNCH - Kernel pc 0x%016lx - "
                    "Kernel name %s - grid launch id %ld - grid size %d,%d,%d "
                    "- block size %d,%d,%d - nregs %d - shmem %d - cuda stream "
                    "id %ld\n",
                    (uint64_t)ctx, pc, func_name, grid_launch_id, p->gridDimX,
                    p->gridDimY, p->gridDimZ, p->blockDimX, p->blockDimY,
                    p->blockDimZ, nregs,
                    shmem_static_nbytes + p->sharedMemBytes,
                    (uint64_t)p->hStream);
            }

        }
        else
        {
            // make sure user kernel finishes to avoid deadlock
            cudaDeviceSynchronize();
            /* push a flush channel kernel */
            flush_channel<<<1, 1>>>(ctx_state->channel_dev);

            /* Make sure GPU is idle */
            cudaDeviceSynchronize();
            assert(cudaGetLastError() == cudaSuccess);

            /* increment grid launch id for next launch */
            grid_launch_id++;
        }
    }
    skip_callback_flag = false;
    pthread_mutex_unlock(&cuda_event_mutex);
}

void* recv_thread_fun(void* args) {
    CUcontext ctx = (CUcontext)args;

    pthread_mutex_lock(&mutex);
    /* get context state from map */
    assert(ctx_state_map.find(ctx) != ctx_state_map.end());
    CTXstate* ctx_state = ctx_state_map[ctx];

    ChannelHost* ch_host = &ctx_state->channel_host;
    pthread_mutex_unlock(&mutex);
    char* recv_buffer = (char*)malloc(CHANNEL_SIZE);

    while (!ctx_state->recv_thread_done) {
        /* receive buffer from channel */
        uint32_t num_recv_bytes = ch_host->recv(recv_buffer, CHANNEL_SIZE);
        if (num_recv_bytes > 0) {
            uint32_t num_processed_bytes = 0;
            while (num_processed_bytes < num_recv_bytes) {
                mem_access_t* ma =
                    (mem_access_t*)&recv_buffer[num_processed_bytes];

#if 0
                std::stringstream ss;
                ss << "CTX " << HEX(ctx) << " - grid_launch_id "
                   << ma->grid_launch_id << " - CTA " << ma->cta_id_x << ","
                   << ma->cta_id_y << "," << ma->cta_id_z << " - warp "
                   << ma->warp_id << " - " << id_to_opcode_map[ma->opcode_id]
                   << " - iaddr " << HEX(ma->iaddr) << " - ";

                for (int i = 0; i < 32; i++) {
                    ss << HEX(ma->addrs[i]) << " ";
                }

                printf("GSNV_TRACE: %s\n", ss.str().c_str());
#endif
                num_processed_bytes += sizeof(mem_access_t);

                try
                {
                    // Handle trace update here
                    mp->handle_cta_memory_access(ma);
                }
                catch (const std::exception & ex)
                {
                    std::cerr << "ERROR: " << ex.what() << std::endl;
                }
            }
        }
    }
    ctx_state->recv_thread_done = false;
    free(recv_buffer);
    return NULL;
}

void nvbit_at_ctx_init(CUcontext ctx) {
    pthread_mutex_lock(&mutex);
    //if (verbose) {
    if (1) {
        printf("GSNV_TRACE: STARTING CONTEXT %p\n", ctx);
    }
    assert(ctx_state_map.find(ctx) == ctx_state_map.end());
    CTXstate* ctx_state = new CTXstate;
    ctx_state_map[ctx] = ctx_state;
    pthread_mutex_unlock(&mutex);

    // -- init #2 - whats the difference
    try {
        if (!gsnv_config_file.empty()) {
            mp->set_config_file(gsnv_config_file);
        }
    }
    catch (const std::exception & ex) {
        std::cerr << "ERROR: " << ex.what() << std::endl;
    }
}

void nvbit_tool_init(CUcontext ctx) {
    pthread_mutex_lock(&mutex);
    assert(ctx_state_map.find(ctx) != ctx_state_map.end());
    init_context_state(ctx);
    pthread_mutex_unlock(&mutex);
}

void nvbit_at_ctx_term(CUcontext ctx) {
    pthread_mutex_lock(&mutex);
    skip_callback_flag = true;
    //if (verbose) {
    if (1) {
        printf("GSNV_TRACE: TERMINATING CONTEXT %p\n", ctx);
    }
    /* get context state from map */
    assert(ctx_state_map.find(ctx) != ctx_state_map.end());
    CTXstate* ctx_state = ctx_state_map[ctx];

    /* Notify receiver thread and wait for receiver thread to
     * notify back */
    ctx_state->recv_thread_done = true;
    while (!ctx_state->recv_thread_done)
        ;

    ctx_state->channel_host.destroy(false);
    cudaFree(ctx_state->channel_dev);
    skip_callback_flag = false;
    delete ctx_state;
    pthread_mutex_unlock(&mutex);

    try
    {
        // Generate GS Pattern output fle
        mp->generate_patterns();
    }
    catch (const std::exception & ex)
    {
        std::cerr << "ERROR: " << ex.what() << std::endl;
    }
}
