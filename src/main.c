/*
 * Copyright (c) 2019, Martin Ichilevici de Oliveira
 *
 * Based on the p2pmem test suite, by Raithlin Consulting Inc.
 * Original copyright notice:
 * Raithlin Consulting Inc. p2pmem test suite
 * Copyright (c) 2017, Raithlin Consulting Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <argconfig/argconfig.h>
#include <argconfig/report.h>
#include <argconfig/suffix.h>
#include <argconfig/timing.h>

#include <ebpf-offload.h>

#include "version.h"

#define KB 1024
#define MB (1024*1024)

#define EBPF_SIZE                 (16*MB)
#define EBPF_PROG_LEN_OFFSET      0x0
#define EBPF_MEM_LEN_OFFSET       0x4
#define EBPF_PROG_OFFSET          0x1000
#define EBPF_CONTROL_PROG_OFFSET  0x100000
#define EBPF_RET_OFFSET           0x200000
#define EBPF_READY_OFFSET         0x200004
#define EBPF_REGS_OFFSET          0x200008
#define EBPF_MEM_OFFSET           0x800000
#define EBPF_START                0x1
#define EBPF_NOT_READY            0x0
#define EBPF_READY                0x1


#define min(a, b)                \
    ({ __typeof__ (a) _a = (a);        \
        __typeof__ (b) _b = (b);    \
        _a < _b ? _a : _b; })

#define max(a, b)                \
    ({ __typeof__ (a) _a = (a);        \
        __typeof__ (b) _b = (b);    \
        _a > _b ? _a : _b; })

const char *def_str = "default string";
const char *desc = "Perform p2pmem and NVMe CMB testing (ver=" VERSION ")";

static struct {
    const char *nvme;
    const char *p2pmem;
    const char *ebpf;
    const char *prog;
    const char *data;
    void     *p2pmem_buffer;
    size_t   p2pmem_size;
    char     *ebpf_buffer;
    size_t   ebpf_size;
    size_t   chunk_size;
    size_t   chunks;
    struct timeval time_start;
    struct timeval time_end;
} cfg = {
    .chunk_size     = 4096,
    .chunks         = 10,
    .ebpf_size      = EBPF_SIZE,
};


int main(int argc, char **argv)
{
    int result[10];
    const struct argconfig_options opts[] = {
        {"nvme", .cfg_type=CFG_STRING,
         .value_addr=&cfg.nvme,
         .argument_type=required_argument,
         .help="NVMe device to read"},
        {"p2pmem", .cfg_type=CFG_STRING,
         .value_addr=&cfg.p2pmem,
         .argument_type=required_argument,
         .help="p2pmem device to use as buffer (omit for sys memory)"},
        {"ebpf", .cfg_type=CFG_STRING,
         .value_addr=&cfg.ebpf,
         .argument_type=required_argument,
         .help="device to offload eBPF program"},
        {"prog", 'p', "", .cfg_type=CFG_STRING,
         .value_addr=&cfg.prog,
         .argument_type=required_argument,
         .help="compiled eBPF code to be offloaded"},
        {"data", 'd', "", .cfg_type=CFG_STRING,
         .value_addr=&cfg.data,
         .argument_type=required_argument,
         .help="data file to be written to the NVMe SSD before starting the eBPF program."},
        {"ebpf_size", .cfg_type=CFG_SIZE,
        .value_addr=&cfg.ebpf_size, .argument_type=required_argument,
        .help="eBPF device size (in bytes)"},
        {"chunks", 'c', "", CFG_LONG_SUFFIX, &cfg.chunks, required_argument,
         "number of chunks to transfer"},
        {"chunk_size", 's', "", CFG_LONG_SUFFIX, &cfg.chunk_size, required_argument,
         "size of data chunk"},
        {NULL}
    };

    argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

    fprintf(stdout,"Running ebpf-test. Parameters:\n");
    fprintf(stdout,"NVMe device: %s\n", cfg.nvme);
    fprintf(stdout,"p2pmem device: %s\n", cfg.p2pmem);
    fprintf(stdout,"eBPF device: %s\n",cfg.ebpf);
    fprintf(stdout,"eBPF program: %s\n", cfg.prog);
    fprintf(stdout,"data file: %s\n", cfg.data);
    fprintf(stdout,"number of chunks: %zd\n", cfg.chunks);
    fprintf(stdout,"chunk size: %zd\n", cfg.chunk_size);

    struct ebpf_offload *ebpf = ebpf_create();
    ebpf_set_nvme(ebpf, cfg.nvme);
    ebpf_set_p2pmem(ebpf, cfg.p2pmem);
    ebpf_set_ebpf(ebpf, cfg.ebpf, cfg.ebpf_size);
    ebpf_set_prog(ebpf, cfg.prog);
    ebpf_set_data(ebpf, cfg.data);
    ebpf_set_chunks(ebpf, cfg.chunks);
    ebpf_set_chunk_size(ebpf, cfg.chunk_size);
    ebpf_use_raw_io(ebpf, false);

    ebpf_init(ebpf);

    gettimeofday(&cfg.time_start, NULL);
    ebpf_run(ebpf, result);
    gettimeofday(&cfg.time_end, NULL);

    fprintf(stdout, "\nIter\tResult\n");
    for (int i = 0; i < 10; i++) {
        fprintf(stdout, "%d\t0x%08x\n", i, result[i]);
    }

    double elapsed_time = timeval_to_secs(&cfg.time_end) -
        timeval_to_secs(&cfg.time_start);
    fprintf(stdout, "Elapsed time: %lfs\n", elapsed_time);

    ebpf_destroy(ebpf);

    return EXIT_SUCCESS;
}
