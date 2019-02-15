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
    int nvme_fd;
    const char *nvme_filename;
    int p2pmem_fd;
    const char *p2pmem_filename;
    int ebpf_fd;
    const char *ebpf_filename;
    int prog_fd;
    const char *prog_filename;
    int data_fd;
    const char *data_filename;
    void     *p2pmem_buffer;
    size_t   p2pmem_size;
    char     *ebpf_buffer;
    size_t   ebpf_size;
    size_t   chunk_size;
    size_t   chunks;
    long     page_size;
    uint64_t rsize;
    struct timeval time_start;
    struct timeval time_end;
} cfg = {
    .chunk_size     = 4096,
    .chunks         = 10,
    .ebpf_size      = EBPF_SIZE,
};

static int execute()
{
    int *control_prog_ptr = (int32_t*) (cfg.ebpf_buffer + EBPF_CONTROL_PROG_OFFSET);
    volatile int *ready_ptr = (int32_t*) (cfg.ebpf_buffer + EBPF_READY_OFFSET);
    volatile int *ret_ptr = (int32_t*) (cfg.ebpf_buffer + EBPF_RET_OFFSET);

    *ready_ptr = EBPF_NOT_READY;
    *control_prog_ptr = EBPF_READY;

    /* Wait until eBPF program finishes */
    while (!*ready_ptr);

    return *ret_ptr;
}

/* Copy the 'data' file to offset 0 of the NVMe SSD */
static void write_data()
{
    void *buf = mmap(NULL, cfg.p2pmem_size, PROT_READ, MAP_SHARED, cfg.data_fd, 0);
    if (buf == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    ssize_t count = write(cfg.nvme_fd, buf, cfg.p2pmem_size);
    if (count != cfg.p2pmem_size) {
        if (count == -1) {
            perror("write");
        }
        fprintf(stderr, "Copying %s to %s failed. Wanted: %lu bytes. Transferred: %lu\n",
                cfg.data_filename, cfg.nvme_filename, cfg.p2pmem_size, count);
        exit(EXIT_FAILURE);
    }
    munmap(buf, cfg.p2pmem_size);
}

/* Write program (and it's length) to the eBPF device */
static void load_program()
{
    /* Get program size */
    int size = lseek(cfg.prog_fd, 0, SEEK_END);
    lseek(cfg.prog_fd, 0, SEEK_SET);

    int* prog_len_ptr = (int32_t*) (cfg.ebpf_buffer + EBPF_PROG_LEN_OFFSET);
    void *prog_ptr = cfg.ebpf_buffer + EBPF_PROG_OFFSET;

    /* Write to device */
    *prog_len_ptr = size;
    size_t bytes = read(cfg.prog_fd, prog_ptr, size);
    if (bytes != size) {
        fprintf(stderr, "Copying %s to %s failed. Program length: %d. Bytes transferred: %lu.\n",
                cfg.prog_filename, cfg.ebpf_filename, size, bytes);
        exit(EXIT_FAILURE);
    }
}

/* DMA 'chunk_size' bytes from the NVMe SSD to the p2pmem device */
static void load_data(int offset)
{
    ssize_t count = pread(cfg.nvme_fd, cfg.p2pmem_buffer, cfg.chunk_size, offset);
    if (count != cfg.chunk_size) {
        if (count == -1) {
            perror("pread");
        }
        fprintf(stderr, "DMAing to %s failed. Chunk size: %lu. Bytes transferred: %lu",
                cfg.nvme_filename, cfg.chunk_size, count);
        exit(EXIT_FAILURE);
    }

    ssize_t* mem_len_ptr = (ssize_t*) (cfg.ebpf_buffer + EBPF_MEM_LEN_OFFSET);
    *mem_len_ptr = count;
    *mem_len_ptr = 17;
}

static void run()
{
    if (cfg.data_fd)
        write_data();
    load_program();
    fprintf(stdout, "\nIter\tResult\n");
    for (int i = 0; i < cfg.chunks; i++) {
        if (cfg.data_fd)
            load_data(i * cfg.chunk_size);
        int result = execute();
        fprintf(stdout, "%d\t0x%08x\n", i, result);
    }
}

int main(int argc, char **argv)
{
    const struct argconfig_options opts[] = {
        {"nvme", .cfg_type=CFG_FD_RDWR_DIRECT_NC,
         .value_addr=&cfg.nvme_fd,
         .argument_type=required_positional,
         .force_default="/dev/nvme0n1",
         .help="NVMe device to read"},
        {"p2pmem", .cfg_type=CFG_FD_RDWR_NC,
         .value_addr=&cfg.p2pmem_fd,
         .argument_type=required_positional,
         .help="p2pmem device to use as buffer (omit for sys memory)"},
        {"ebpf", .cfg_type=CFG_FD_RDWR_NC,
         .value_addr=&cfg.ebpf_fd,
         .argument_type=required_positional,
         .help="device to offload eBPF program"},
        {"prog", 'p', "", .cfg_type=CFG_FD_RD,
         .value_addr=&cfg.prog_fd,
         .argument_type=required_argument,
         .help="compiled eBPF code to be offloaded"},
        {"data", 'd', "", .cfg_type=CFG_FD_RD,
         .value_addr=&cfg.data_fd,
         .argument_type=required_argument,
         .help="data file to be written to the NVMe SSD before starting the eBPF program."},
        {"chunks", 'c', "", CFG_LONG_SUFFIX, &cfg.chunks, required_argument,
         "number of chunks to transfer"},
        {"chunk_size", 's', "", CFG_LONG_SUFFIX, &cfg.chunk_size, required_argument,
         "size of data chunk"},
        {NULL}
    };

    argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));
    cfg.page_size = sysconf(_SC_PAGESIZE);
    cfg.p2pmem_size = cfg.chunk_size * cfg.chunks;;

    if (ioctl(cfg.nvme_fd, BLKGETSIZE64, &cfg.rsize)) {
        perror("ioctl-read");
        goto fail_out;
    }

    if (cfg.p2pmem_fd && (cfg.chunk_size % cfg.page_size)){
        fprintf(stderr, "--size must be a multiple of PAGE_SIZE in p2pmem mode.\n");
        goto fail_out;
    }

    cfg.p2pmem_buffer = mmap(NULL, cfg.p2pmem_size, PROT_READ | PROT_WRITE, MAP_SHARED,
              cfg.p2pmem_fd, 0);

    cfg.ebpf_buffer = mmap(NULL, cfg.ebpf_size, PROT_READ | PROT_WRITE, MAP_SHARED, cfg.ebpf_fd, 0);

    fprintf(stdout,"Running ebpf-test. Parameters:\n");
    fprintf(stdout,"NVMe device: %s\n", cfg.nvme_filename);
    fprintf(stdout,"p2pmem device: %s\n", cfg.p2pmem_filename);
    fprintf(stdout,"eBPF device: %s\n",cfg.ebpf_filename);
    fprintf(stdout,"eBPF program: %s\n", cfg.prog_filename);
    fprintf(stdout,"data file: %s\n", cfg.data_filename);
    fprintf(stdout,"number of chunks: %zd\n", cfg.chunks);
    fprintf(stdout,"chunk size: %zd\n", cfg.chunk_size);

    gettimeofday(&cfg.time_start, NULL);
    run();
    gettimeofday(&cfg.time_end, NULL);

    double elapsed_time = timeval_to_secs(&cfg.time_end) -
        timeval_to_secs(&cfg.time_start);
    fprintf(stdout, "Elapsed time: %lfs\n", elapsed_time);

    munmap(cfg.p2pmem_buffer, cfg.p2pmem_size);
    munmap(cfg.ebpf_buffer, cfg.ebpf_size);

    return EXIT_SUCCESS;

fail_out:
    return EXIT_FAILURE;
}
