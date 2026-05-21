##===- Makefile -----------------------------------------------------------===##
# 
# This file licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (C) 2024-2026, Advanced Micro Devices, Inc.
# 
##===----------------------------------------------------------------------===##

srcdir := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

#include ${srcdir}/../../makefile-common
#####

getwslpath = echo
srcdir := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
MLIR_AIE_DIR ?= $(shell python3 -c "from aie.utils.config import root_path; print(root_path())")

VPATH := ${srcdir}/../../kernels/

MLIR_AIE_DIR ?= $(shell python3 -c "from aie.utils.config import root_path; print(root_path())")

WARNING_FLAGS = -Wno-parentheses -Wno-attributes -Wno-macro-redefined -Wno-empty-body -Wno-missing-template-arg-list-after-template-kw

PEANOWRAP2_FLAGS = -O2 -std=c++20 --target=aie2-none-unknown-elf ${WARNING_FLAGS} -DNDEBUG -I ${MLIR_AIE_DIR}/include
PEANOWRAP2P_FLAGS = -O2 -std=c++20 --target=aie2p-none-unknown-elf ${WARNING_FLAGS} -DNDEBUG -I ${MLIR_AIE_DIR}/include

####

scalar?=0
# Custom Flags for kernel
ifeq ($(scalar), 1)
	PEANOWRAP2_FLAGS += -DALIGN_SCALAR
	PEANOWRAP2P_FLAGS += -DALIGN_SCALAR
endif


devicename ?= $(if $(filter 1,$(NPU2)),npu2,npu)
targetname = align_npu
trace_size = 8192
CHESS ?= false

verbose ?= 0 # acceptable values 1 and 2 (more verbose)

aie_py_src=${targetname}_placed.py

# Space-separated list of ref_len:qry_len size pairs to build xclbins for.
# Builds for tests suite. Specify which type if you want to only do specific ones.
TEST_SIZES ?= 7:7 8:16 16:16 16:17 32:32 32:64
	# 80:80 88:88 60:132  Should not pass as it warns a failed to allocate buffer, yet these work
	# 244:32 Does not work at all

# Derive all per-size xclbin paths
xclbin_targets := $(foreach sz,$(TEST_SIZES),build/$(subst :,x,$(sz))/final.xclbin)

.PHONY: all template clean

all: $(xclbin_targets) ${targetname}.exe

# --- Per-size kernel object (shared across all sizes since the AIE kernel is size-agnostic) ---
build/align_u16.cc.o: align_u16.cc
	mkdir -p ${@D}
ifeq ($(devicename),npu)
	cd ${@D} && ${PEANO_INSTALL_DIR}/bin/clang++ ${PEANOWRAP2_FLAGS} -DBIT_WIDTH=8 -c $< -o ${@F}
else ifeq ($(devicename),npu2)
	cd ${@D} && ${PEANO_INSTALL_DIR}/bin/clang++ ${PEANOWRAP2P_FLAGS} -DBIT_WIDTH=8 -c $< -o ${@F}
else
	$(error Device type not supported)
endif

# --- Per-size MLIR generation and xclbin compilation ---
# $(1) = "ref_len:qry_len", e.g. "32:32"
define build_size_targets
build/$(subst :,x,$(1))/aie2_lineBased_8b.mlir: ${srcdir}/${aie_py_src}
	mkdir -p $$(@D)
	python3 $$< -d ${devicename} \
		--ref-len $(word 1,$(subst :, ,$(1))) \
		--qry-len $(word 2,$(subst :, ,$(1))) > $$@

build/$(subst :,x,$(1))/final.xclbin: build/$(subst :,x,$(1))/aie2_lineBased_8b.mlir build/align_u16.cc.o
	mkdir -p $$(@D)
	cp build/align_u16.cc.o $$(@D)/
	cd $$(@D) && aiecc --aie-generate-xclbin --aie-generate-npu-insts \
		--no-xchesscc --no-xbridge \
		--xclbin-name=final.xclbin --npu-insts-name=insts.bin \
		$(CURDIR)/build/$(subst :,x,$(1))/aie2_lineBased_8b.mlir
endef

$(foreach sz,$(TEST_SIZES),$(eval $(call build_size_targets,$(sz))))

# --- Host binary (built once, size-agnostic at compile time) ---
${targetname}.exe: ${srcdir}/test.cpp
	rm -rf _build
	mkdir -p _build
	cd _build && ${powershell} cmake `${getwslpath} ${srcdir}` -DTARGET_NAME=${targetname}
	cd _build && ${powershell} cmake --build . --config Release
ifeq "${powershell}" "powershell.exe"
	cp _build/${targetname}.exe $@
else
	cp _build/${targetname} $@
endif

run: ${targetname}.exe $(xclbin_targets)
	${powershell} ./$< -k MLIR_AIE --verbosity ${verbose}

clean:
	rm -rf build _build ${targetname}.exe
