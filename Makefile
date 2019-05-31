########################################################################
##
## Raithlin Consulting Inc. p2pmem test suite
## Copyright (c) 2017, Raithlin Consulting Inc.
##
## This program is free software; you can redistribute it and/or modify it
## under the terms and conditions of the GNU General Public License,
## version 2, as published by the Free Software Foundation.
##
## This program is distributed in the hope it will be useful, but WITHOUT
## ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
## FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
## more details.
##
########################################################################

OBJDIR=build

DESTDIR ?=

LIBARGCONFIGDIR=libargconfig

CPPFLAGS=-Iinc -Ibuild
CFLAGS=-std=gnu99 -g -O2 -fPIC -Wall -Werror -I$(LIBARGCONFIGDIR)/inc
DEPFLAGS= -MT $@ -MMD -MP -MF $(OBJDIR)/$*.d
LDLIBS+= -lebpf-offload

EXE=ebpf-test
SRCS=$(wildcard src/*.c)
OBJS=$(addprefix $(OBJDIR)/, $(patsubst %.c,%.o, $(SRCS)))

CLANG=clang

CHECK_NVME=/dev/nvme0n1
CHECK_P2PMEM=/dev/p2pmem0
CHECK_PCI_UBPF=/dev/pci_ubpf0

TESTDIR=test

ifneq ($(V), 1)
Q=@
MAKEFLAGS+=-s --no-print-directory
else
NQ=:
endif

compile: $(EXE)

check: check_simple check_count

clean:
	@$(NQ) echo "  CLEAN  $(EXE)"
	$(Q)rm -rf $(EXE) build *~ ./src/*~ $(TESTDIR)/*.o $(TESTDIR)/*.out
	$(Q)$(MAKE) -C $(LIBARGCONFIGDIR) clean

check_simple: $(EXE) test/simple.o test/simple.dat
	sudo ./$(EXE) --nvme $(CHECK_NVME) --p2pmem $(CHECK_P2PMEM) --ebpf $(CHECK_PCI_UBPF) \
		--prog test/simple.o --data test/simple.dat \
		--chunk_size 4096 --chunks 10 > $(TESTDIR)/simple.out
	if diff $(TESTDIR)/simple.ans $(TESTDIR)/simple.out -q -I "Elapsed.*" >/dev/null ;\
	then \
		echo "  CHECK Pass" ;\
	else \
		echo "  CHECK Fail" ; false ; \
	fi

check_count: $(EXE) $(TESTDIR)/count.o test/count.dat
	sudo ./$(EXE) --nvme $(CHECK_NVME) --p2pmem $(CHECK_P2PMEM) --ebpf $(CHECK_PCI_UBPF) \
		--prog test/count.o --data test/count.dat \
		--chunk_size `stat -c %s test/count.dat` --chunks 1 > $(TESTDIR)/count.out
	if diff $(TESTDIR)/count.ans $(TESTDIR)/count.out -q -I "Elapsed.*" >/dev/null ;\
	then \
		echo "  CHECK Pass" ;\
	else \
		echo "  CHECK Fail" ; false ; \
	fi

run: $(EXE) $(CHECK_PROG_OBJ) $(CHECK_DATA)
	sudo ./$(EXE) --nvme $(CHECK_NVME) --p2pmem $(CHECK_P2PMEM) --ebpf $(CHECK_PCI_UBPF) \
		--prog $(CHECK_PROG_OBJ) --data $(CHECK_DATA) \
		--chunk_size $(CHECK_CHUNK_SIZE) --chunks $(CHECK_CHUNKS)

debug: $(EXE) $(CHECK_PROG_OBJ) $(CHECK_DATA)
	sudo gdb --args ./$(EXE) --nvme $(CHECK_NVME) --p2pmem $(CHECK_P2PMEM) --ebpf $(CHECK_PCI_UBPF) \
		--prog $(CHECK_PROG_OBJ) --data $(CHECK_DATA) \
		--chunk_size $(CHECK_CHUNK_SIZE) --chunks $(CHECK_CHUNKS)

valgrind: $(EXE) $(CHECK_PROG_OBJ) $(CHECK_DATA)
	sudo valgrind ./$(EXE) --nvme $(CHECK_NVME) --p2pmem $(CHECK_P2PMEM) --ebpf $(CHECK_PCI_UBPF) \
		--prog $(CHECK_PROG_OBJ) --data $(CHECK_DATA) \
		--chunk_size $(CHECK_CHUNK_SIZE) --chunks $(CHECK_CHUNKS)


$(TESTDIR)/%.o: $(TESTDIR)/%.c
	$(CLANG) -target bpf -c $^ -o $@

$(OBJDIR)/version.h $(OBJDIR)/version.mk: FORCE $(OBJDIR)
	@$(SHELL_PATH) ./VERSION-GEN
$(OBJDIR)/src/main.o: $(OBJDIR)/version.h
-include $(OBJDIR)/version.mk

$(OBJDIR):
	$(Q)mkdir -p $(OBJDIR)/src

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@$(NQ) echo "  CC     $<"
	$(Q)$(COMPILE.c) $(DEPFLAGS) $< -o $@

$(LIBARGCONFIGDIR)/libargconfig.a: FORCE
	@$(NQ) echo "  MAKE   $@"
	$(Q)$(MAKE) -C $(LIBARGCONFIGDIR)

$(EXE): $(OBJS) $(LIBARGCONFIGDIR)/libargconfig.a
	@$(NQ) echo "  LD     $@"
	$(Q)$(LINK.o) $^ $(LDLIBS) -o $@

.PHONY: clean compile FORCE check

-include $(patsubst %.o,%.d,$(OBJS))
