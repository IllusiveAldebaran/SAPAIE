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

# Custom Flags for kernel
PEANOWRAP2_FLAGS += -DALIGN_SCALAR
PEANOWRAP2P_FLAGS += -DALIGN_SCALAR

devicename ?= $(if $(filter 1,$(NPU2)),npu2,npu)
targetname = align_npu
ref_len   ?= 16
qry_len   ?= 16
in1_size   = $(ref_len)
in2_size   = $(qry_len)
# out_size: (qry_len+1)*(ref_len+1) rounded up to even uint16, times 2 bytes
out_size   = $(shell python3 -c "r=$(ref_len);q=$(qry_len);n=(q+1)*(r+1);print(((n+1)//2)*2*2)")
trace_size = 8192
CHESS ?= false
CHESS ?= false

verbose ?= 0 # acceptable values 1 and 2 (more verbose)

aie_py_src=${targetname}_placed.py

.PHONY: all template clean

all: build/final.xclbin

# build npu kernels
build/%.cc.o: %.cc
	mkdir -p ${@D}
ifeq ($(devicename),npu)
	cd ${@D} && ${PEANO_INSTALL_DIR}/bin/clang++ ${PEANOWRAP2_FLAGS} -DBIT_WIDTH=8 -c $< -o ${@F}
else ifeq ($(devicename),npu2)
	cd ${@D} && ${PEANO_INSTALL_DIR}/bin/clang++ ${PEANOWRAP2P_FLAGS} -DBIT_WIDTH=8 -c $< -o ${@F}
else
	echo "Device type not supported"
endif

build/aie2_lineBased_8b.mlir: ${srcdir}/${aie_py_src}
	mkdir -p ${@D}
	python3 $< -d ${devicename} --ref-len ${ref_len} --qry-len ${qry_len} > $@

build/aie2_trace_lineBased_8b.mlir: ${srcdir}/${aie_py_src}
	mkdir -p ${@D}
	python3 $< -d ${devicename} -t ${trace_size} --ref-len ${ref_len} --qry-len ${qry_len} > $@

build/final.xclbin: build/aie2_lineBased_8b.mlir build/align_u16.cc.o
	mkdir -p ${@D}
	cd ${@D} && aiecc --aie-generate-xclbin --aie-generate-npu-insts  \
		--no-xchesscc --no-xbridge \
		--xclbin-name=${@F} --npu-insts-name=insts.bin $(<:%=../%)

build/final_trace.xclbin: build/aie2_trace_lineBased_8b.mlir build/align_u16.cc.o
	mkdir -p ${@D}
	cd ${@D} && aiecc --aie-generate-xclbin --aie-generate-npu-insts  \
		--no-xchesscc --no-xbridge \
		--xclbin-name=${@F} --npu-insts-name=insts.bin $(<:%=../%)


${targetname}.exe: ${srcdir}/test.cpp
	rm -rf _build
	mkdir -p _build
	cd _build && ${powershell} cmake `${getwslpath} ${srcdir}` -DTARGET_NAME=${targetname} \
		-DIN1_SIZE=${in1_size} -DIN2_SIZE=${in2_size} -DOUT_SIZE=${out_size}
	cd _build && ${powershell} cmake --build . --config Release
ifeq "${powershell}" "powershell.exe"
	cp _build/${targetname}.exe $@
else
	cp _build/${targetname} $@
endif

run: ${targetname}.exe build/final.xclbin build/insts.bin
	${powershell} ./$< -x build/final.xclbin -i build/insts.bin -k MLIR_AIE --verbosity ${verbose}

run_py: build/final.xclbin build/insts.bin
	${powershell} python3 ${srcdir}/test.py -x build/final.xclbin -i build/insts.bin -k MLIR_AIE

trace: ${targetname}.exe build/final_trace.xclbin build/insts.bin
	${powershell} ./$< -x build/final_trace.xclbin -i build/insts.bin -k MLIR_AIE -t ${trace_size}
	${srcdir}/../../../python/utils/trace/parse.py --input trace.txt --mlir build/aie2_trace_lineBased_8b.mlir --output trace_align.json
	${srcdir}/../../../python/utils/trace/get_trace_summary.py --input trace_align.json

trace_py: build/final_trace.xclbin build/insts.bin
	${powershell} python3 ${srcdir}/test.py -x build/final_trace.xclbin -i build/insts.bin -k MLIR_AIE -t ${trace_size}
	${srcdir}/../../../python/utils/trace/parse.py --input trace.txt --mlir build/aie2_trace_lineBased_8b.mlir --output trace_align.json
	${srcdir}/../../../python/utils/trace/get_trace_summary.py --input trace_align.json

clean_trace:
	rm -rf tmpTrace trace.txt parse*json trace*json

clean: clean_trace
	rm -rf build _build ${targetname}.exe
