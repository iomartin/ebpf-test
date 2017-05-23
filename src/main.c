/*
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <argconfig/argconfig.h>

#include "version.h"

const char *desc = "Perform p2pmem and NVMe CMB testing (ver=" VERSION ")";

static struct {
	int nvme_read_fd;
	const char *nvme_read_filename;
	int nvme_write_fd;
	const char *nvme_write_filename;
	int p2pmem_fd;
	const char *p2pmem_filename;
	unsigned check;
	size_t   chunks;
	long     page_size;
	int      seed;
	size_t   size;
	int      write_parity;
	int      read_parity;
} cfg = {
	.check  = 0,
	.chunks = 1024,
	.seed   = -1,
	.size   = 4096,
};

static int writedata()
{
	int *buffer;
	ssize_t count;
	int ret = 0;

	if (posix_memalign((void **)&buffer, cfg.page_size, cfg.size*cfg.chunks))
		return -1;

	cfg.write_parity = 0;
	for (size_t i=0; i<(cfg.size*cfg.chunks/sizeof(int)); i++) {
		buffer[i] = rand();
		cfg.write_parity ^= buffer[i];
	}
	count = write(cfg.nvme_read_fd, (void *)buffer, cfg.size*cfg.chunks);
	if (count == -1){
		ret = -1;
		goto out;
	}

out:
	free(buffer);
	return ret;
}

static int readdata()
{
	int *buffer;
	ssize_t count;
	int ret = 0;

	if (posix_memalign((void **)&buffer, cfg.page_size, cfg.size*cfg.chunks))
		return -1;

	count = read(cfg.nvme_write_fd, (void *)buffer, cfg.size*cfg.chunks);
	if (count == -1) {
		ret = -1;
		goto out;
	}

	cfg.read_parity = 0;
	for (size_t i=0; i<(cfg.size*cfg.chunks/sizeof(int)); i++)
		cfg.read_parity ^= buffer[i];

out:
	free(buffer);
	return ret;
}


int main(int argc, char **argv)
{
	void *p2pmem;
	ssize_t count;

	const struct argconfig_options opts[] = {
		{"nvme-read", .cfg_type=CFG_FD_RDWR_DIRECT,
		 .value_addr=&cfg.nvme_read_fd,
		 .argument_type=required_positional,
		 .force_default="/dev/nvme0n1",
		 .help="NVMe device to read"},
		{"nvme-write", .cfg_type=CFG_FD_RDWR_DIRECT,
		 .value_addr=&cfg.nvme_write_fd,
		 .argument_type=required_positional,
		 .force_default="/dev/nvme1n1",
		 .help="NVMe device to write"},
		{"p2pmem", .cfg_type=CFG_FD_RDWR,
		 .value_addr=&cfg.p2pmem_fd,
		 .argument_type=required_positional,
		 .force_default="/dev/p2pmem0",
		 .help="p2pmem device to use as buffer"},
		{"check", 'a', "", CFG_NONE, &cfg.check, no_argument,
		 "perform checksum check on transfer (slow)"},
		{"chunks", 'c', "", CFG_SIZE, &cfg.chunks, required_argument,
		 "number of chunks to transfer"},
		{"seed", 'b', "", CFG_INT, &cfg.seed, required_argument,
		 "seed to use for random data (-1 for time based)"},
		{"size", 's', "", CFG_SIZE, &cfg.size, required_argument,
		 "size of data chunk"},
		{NULL}
	};

	argconfig_parse(argc, argv, desc, opts, &cfg, sizeof(cfg));

	p2pmem = mmap(NULL, cfg.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      cfg.p2pmem_fd, 0);
	if (p2pmem == MAP_FAILED)
		perror("mmap");

	if ( cfg.seed == -1 )
		cfg.seed = time(NULL);
	srand(cfg.seed);

	cfg.page_size = sysconf(_SC_PAGESIZE);

	fprintf(stdout,"Running p2pmem-test: Reading %s : Writing %s : "
		"p2pmem Buffer %s.\n",cfg.nvme_read_filename, cfg.nvme_write_filename,
		cfg.p2pmem_filename);
	fprintf(stdout,"\tchunk size = %zd : number of chunks =  %zd: total = %zd.\n",
		cfg.size, cfg.chunks, cfg.size*cfg.chunks);
	fprintf(stdout,"\tp2pmem = %p\n", p2pmem);
	fprintf(stdout,"\tPAGE_SIZE = %ldB\n", cfg.page_size);
	if (cfg.check)
		fprintf(stdout,"\tchecking data with seed = %d\n", cfg.seed);

	if (cfg.check)
		if (writedata(cfg))
			perror("writedata");

	if (lseek(cfg.nvme_read_fd, 0, SEEK_SET) == -1)
		perror("writedata-lseek");

	for (size_t i=0; i<cfg.chunks; i++) {

		count = read(cfg.nvme_read_fd, p2pmem, cfg.size);

		if (count == -1)
			perror("read");

		count = write(cfg.nvme_write_fd, p2pmem, cfg.size);

		if (count == -1)
			perror("write");
	}

	if (cfg.check) {
		if (lseek(cfg.nvme_write_fd, 0, SEEK_SET) == -1)
			perror("readdata-lseek");
		if (readdata(cfg))
			perror("readdata");
	}

	if (cfg.check)
		fprintf(stdout, "%s on data check, 0x%x %s 0x%x.\n",
			cfg.write_parity==cfg.read_parity ? "MATCH" : "MISMATCH",
			cfg.write_parity,
			cfg.write_parity==cfg.read_parity ? "=" : "!=",
			cfg.read_parity);

	munmap(p2pmem, cfg.size);

	return 0;
}
