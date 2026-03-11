# SPDX-License-Identifier: GPL-2.0
#
# Makefile for nccl_cpu_observer kernel eBPF program.
#
# Usage:
#   make          -- compile nccl_cpu_observer.bpf.o
#   make clean    -- remove build artifacts
#
# The resulting .bpf.o can be loaded by load.sh or by a libbpf-based
# userspace loader.

# --------------------------------------------------------------------------- #
# Toolchain
# --------------------------------------------------------------------------- #
CLANG   ?= clang
LLVM_STRIP ?= llvm-strip

# --------------------------------------------------------------------------- #
# Architecture detection (matches bpf libbpf/libbpf convention)
# --------------------------------------------------------------------------- #
ARCH ?= $(shell uname -m \
            | sed 's/x86_64/x86/' \
            | sed 's/arm.*/arm/'  \
            | sed 's/aarch64/arm64/' \
            | sed 's/ppc64le/powerpc/' \
            | sed 's/mips.*/mips/'     \
            | sed 's/riscv64/riscv/'   \
            | sed 's/loongarch64/loongarch/')

# --------------------------------------------------------------------------- #
# Include paths
# --------------------------------------------------------------------------- #
# Prefer the vmlinux.h shipped with bpftime (already arch-specific, CO-RE
# enabled).  Fall back to the one in the project root vmlinux/ directory.
REPO_ROOT := $(abspath ../..)

BPFTIME_VMLINUX := $(REPO_ROOT)/bpftime/third_party/vmlinux/$(ARCH)/vmlinux.h
PROJECT_VMLINUX := $(REPO_ROOT)/vmlinux/vmlinux.h

# Pick the first one that exists.
ifeq ($(wildcard $(BPFTIME_VMLINUX)),$(BPFTIME_VMLINUX))
    VMLINUX_H     := $(BPFTIME_VMLINUX)
    VMLINUX_DIR   := $(dir $(BPFTIME_VMLINUX))
else
    VMLINUX_H     := $(PROJECT_VMLINUX)
    VMLINUX_DIR   := $(dir $(PROJECT_VMLINUX))
endif

# libbpf UAPI headers (for bpf_helpers.h, bpf_tracing.h, bpf_core_read.h).
LIBBPF_UAPI := $(REPO_ROOT)/bpftime/third_party/libbpf/src

# Gather clang's default system includes so that asm/types.h etc. are found.
CLANG_BPF_SYS_INCLUDES ?= $(shell $(CLANG) -v -E - </dev/null 2>&1 \
    | sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')

INCLUDES := -I$(VMLINUX_DIR) -I$(LIBBPF_UAPI) -I.

# --------------------------------------------------------------------------- #
# Compiler flags for BPF target
# --------------------------------------------------------------------------- #
BPF_CFLAGS := \
    -g \
    -O2 \
    -target bpf \
    -D__TARGET_ARCH_$(ARCH) \
    -D__BPF_TRACING__ \
    $(INCLUDES) \
    $(CLANG_BPF_SYS_INCLUDES)

# --------------------------------------------------------------------------- #
# Targets
# --------------------------------------------------------------------------- #
OBJ := nccl_cpu_observer.bpf.o

.PHONY: all clean

all: $(OBJ)

$(OBJ): nccl_cpu_observer.bpf.c nccl_cpu_observer.h $(VMLINUX_H)
	$(CLANG) $(BPF_CFLAGS) -c nccl_cpu_observer.bpf.c -o $@
	@# Strip debug sections that are not needed for loading, keeping BTF.
	@# Comment out the next line if you want full DWARF for debugging.
	$(LLVM_STRIP) -g $@ 2>/dev/null || true

clean:
	rm -f $(OBJ)

# Print resolved paths for debugging.
info:
	@echo "ARCH        = $(ARCH)"
	@echo "VMLINUX_H   = $(VMLINUX_H)"
	@echo "LIBBPF_UAPI = $(LIBBPF_UAPI)"
	@echo "CLANG       = $(CLANG) ($(shell $(CLANG) --version 2>&1 | head -1))"
