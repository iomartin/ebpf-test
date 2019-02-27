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
CHECK_PROG=test/test.c
CHECK_PROG_OBJ=$(CHECK_PROG:.c=.o)
CHECK_DATA=test/test.dat
CHECK_CHUNK_SIZE=4096
CHECK_CHUNKS=10
CHECK_OUT=test/test.out
CHECK_ANS=test/test.ans


ifneq ($(V), 1)
Q=@
MAKEFLAGS+=-s --no-print-directory
else
NQ=:
endif

compile: $(EXE)

clean:
	@$(NQ) echo "  CLEAN  $(EXE)"
	$(Q)rm -rf $(EXE) build *~ ./src/*~ $(CHECK_PROG_OBJ) $(CHECK_OUT)
	$(Q)$(MAKE) -C $(LIBARGCONFIGDIR) clean

check: $(EXE) $(CHECK_PROG_OBJ) $(CHECK_DATA)
	sudo ./$(EXE) --nvme $(CHECK_NVME) --p2pmem $(CHECK_P2PMEM) --ebpf $(CHECK_PCI_UBPF) \
		--prog $(CHECK_PROG_OBJ) --data $(CHECK_DATA) \
		--chunk_size $(CHECK_CHUNK_SIZE) --chunks $(CHECK_CHUNKS) > $(CHECK_OUT)
	if diff $(CHECK_OUT) $(CHECK_ANS) -q -I "Elapsed.*" >/dev/null ;\
	then \
		echo "  CHECK Pass" ;\
	else \
		echo "  CHECK Fail" ; false ; \
	fi

$(CHECK_PROG_OBJ): $(CHECK_PROG)
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
