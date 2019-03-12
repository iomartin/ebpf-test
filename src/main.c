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
#include <libgen.h>
#include <linux/fs.h>
#include <mntent.h>
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

const char *desc = "Perform eBPF offloading testing (ver=" VERSION ")";

static struct {
    const char *nvme;
    const char *p2pmem;
    const char *ebpf;
    const char *prog;
    char *data;
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

static char* nvme_mount_point(const char *nvme)
{
    char *mount_point = NULL;
    struct mntent *ent;
    FILE *file = setmntent("/etc/mtab", "r");

    if (!file) {
        perror("Could not open /etc/mtab");
        exit(1);
    }

    while ((ent = getmntent(file)) != NULL) {
        if (strcmp(ent->mnt_fsname, cfg.nvme) == 0) {
            mount_point = malloc(strlen(ent->mnt_fsname) + 1);
            strcpy(mount_point, ent->mnt_dir);
            break;
        }
    }

    endmntent(file);

    return mount_point;
}

int main(int argc, char **argv)
{
    int *result;
    const struct argconfig_options opts[] = {
        {"nvme", .cfg_type=CFG_STRING,
         .value_addr=&cfg.nvme,
         .argument_type=required_argument,
         .help="NVMe device to use. Optional if the device contains a mounted filesystem and the data file is already in the device."},
        {"p2pmem", .cfg_type=CFG_STRING,
         .value_addr=&cfg.p2pmem,
         .argument_type=required_argument,
         .help="p2pmem device to use as buffer."},
        {"ebpf", .cfg_type=CFG_STRING,
         .value_addr=&cfg.ebpf,
         .argument_type=required_argument,
         .help="device to offload eBPF program."},
        {"prog", 'p', "", .cfg_type=CFG_STRING,
         .value_addr=&cfg.prog,
         .argument_type=required_argument,
         .help="compiled eBPF code to be offloaded."},
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

    if (!cfg.p2pmem) {
        fprintf(stderr, "--p2pmem is required\n");
        exit(EXIT_FAILURE);
    }
    if (!cfg.ebpf) {
        fprintf(stderr, "--ebpf is required\n");
        exit(EXIT_FAILURE);
    }
    if (!cfg.prog) {
        fprintf(stderr, "--prog is required\n");
        exit(EXIT_FAILURE);
    }

    result = malloc(cfg.chunks * sizeof(*result));

    fprintf(stdout,"Running ebpf-test. Parameters:\n");
    fprintf(stdout,"NVMe device: %s\n", cfg.nvme);
    fprintf(stdout,"p2pmem device: %s\n", cfg.p2pmem);
    fprintf(stdout,"eBPF device: %s\n",cfg.ebpf);
    fprintf(stdout,"eBPF program: %s\n", cfg.prog);
    fprintf(stdout,"data file: %s\n", cfg.data);
    fprintf(stdout,"number of chunks: %zd\n", cfg.chunks);
    fprintf(stdout,"chunk size: %zd\n", cfg.chunk_size);

    struct ebpf_offload *ebpf = ebpf_create();

    /* Check if we need to copy the data file to the NVMe device.
     * This should be done if both conditions below are met:
     *   - The NVMe device is mounted;
     *   - The file is not in the NVMe device already
     */
    if (cfg.data && cfg.nvme) {
        /* Check if NVMe device is mounted */
        char *mount_point = nvme_mount_point(cfg.nvme);
        if (mount_point) {
            ebpf_use_raw_io(ebpf, false);

            /* Is the data file already in the NVMe device? */
            bool need_to_copy = true;
            char *data_abs_path = realpath(cfg.data, NULL);
            char *path = data_abs_path;
            do {
                path = dirname(path);
                if (strcmp(path, mount_point) == 0) {
                    need_to_copy = false;
                }
            } while(strcmp(path, "/"));
            free(data_abs_path);

            if (need_to_copy) {
                char *name = basename(cfg.data);
                char *new_prog_name = malloc(strlen(mount_point) + strlen("/") + strlen(name) + 1);
                sprintf(new_prog_name, "%s/%s", mount_point, name);

                int new_prog_fd = open(new_prog_name, O_WRONLY | O_CREAT | O_TRUNC);
                int data_fd = open(cfg.data, O_RDONLY);

                char buf[4096];
                int bytes;
                while ((bytes = read(data_fd, buf, 4096)) > 0) {
                    write(new_prog_fd, buf, bytes);
                }
                free(new_prog_name);
            }
            free(mount_point);
        }
        else {
            ebpf_use_raw_io(ebpf, true);
        }
    }

    if (cfg.nvme)
        ebpf_set_nvme(ebpf, cfg.nvme);
    if (cfg.data)
        ebpf_set_data(ebpf, cfg.data);

    ebpf_set_p2pmem(ebpf, cfg.p2pmem);
    ebpf_set_ebpf(ebpf, cfg.ebpf, cfg.ebpf_size);
    ebpf_set_prog(ebpf, cfg.prog);
    ebpf_set_chunks(ebpf, cfg.chunks);
    ebpf_set_chunk_size(ebpf, cfg.chunk_size);

    ebpf_init(ebpf);

    gettimeofday(&cfg.time_start, NULL);
    ebpf_run(ebpf, result);
    gettimeofday(&cfg.time_end, NULL);

    fprintf(stdout, "\nIter\tResult\n");
    for (int i = 0; i < cfg.chunks; i++) {
        fprintf(stdout, "%d\t0x%08x\n", i, result[i]);
    }

    double elapsed_time = timeval_to_secs(&cfg.time_end) -
        timeval_to_secs(&cfg.time_start);
    fprintf(stdout, "Elapsed time: %lfs\n", elapsed_time);

    ebpf_destroy(ebpf);
    free(result);

    return EXIT_SUCCESS;
}
