# passthrough_kernel/passthrough_kernel_placed.py -*- Python -*-
#
# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# (c) Copyright 2024 Advanced Micro Devices, Inc. or its affiliates
import numpy as np
import argparse
import sys

from aie.dialects.aie import *
from aie.dialects.aiex import *
from aie.extras.context import mlir_mod_ctx
from aie.iron.controlflow import range_

import aie.utils.trace as trace_utils


SEQ_LEN = 16
DP_ROWS = SEQ_LEN + 1   # 17
DP_COLS = SEQ_LEN + 1   # 17
# DMA BDs require transfer length to be a multiple of 4 bytes.
# 17*17=289 uint16 = 578 bytes, not 4-byte aligned. Pad to 292 elements (584 bytes).
OUT_ELEMS = ((DP_ROWS * DP_COLS + 3) // 4) * 4  # 292


def my_align_kernel(dev, trace_size):
    in_dtype = np.int8   # char encoded nucleotide
    out_dtype = np.uint16

    in1_ty = np.ndarray[(SEQ_LEN,), np.dtype[in_dtype]]
    in2_ty = np.ndarray[(SEQ_LEN,), np.dtype[in_dtype]]
    out_ty  = np.ndarray[(OUT_ELEMS,), np.dtype[out_dtype]]

    @device(dev)
    def device_body():
        # AIE Core Function declarations
        align_u16 = external_func(
            "align_ch_u16",
            inputs=[in1_ty, in2_ty, out_ty],
            link_with="align_u16.cc.o",
        )

        # Tile declarations
        ShimTile = tile(0, 0)
        ComputeTile2 = tile(0, 2)

        # Set up a packet-switched flow from core to shim for tracing information
        tiles_to_trace = [ComputeTile2, ShimTile]
        if trace_size > 0:
            trace_utils.configure_packet_tracing_flow(tiles_to_trace, ShimTile)

        # AIE-array data movement with object fifos
        of_in1 = object_fifo("in1", ShimTile, ComputeTile2, 2, in1_ty)
        of_in2 = object_fifo("in2", ShimTile, ComputeTile2, 2, in2_ty)
        of_out = object_fifo("out", ComputeTile2, ShimTile, 2, out_ty)

        # Compute tile 2
        @core(ComputeTile2)
        def core_body():
            for _ in range_(sys.maxsize):
                elemOut  = of_out.acquire(ObjectFifoPort.Produce, 1)
                elemIn1  = of_in1.acquire(ObjectFifoPort.Consume, 1)
                elemIn2  = of_in2.acquire(ObjectFifoPort.Consume, 1)
                align_u16(elemIn1, elemIn2, elemOut)
                of_in1.release(ObjectFifoPort.Consume, 1)
                of_in2.release(ObjectFifoPort.Consume, 1)
                of_out.release(ObjectFifoPort.Produce, 1)

        @runtime_sequence(in1_ty, in2_ty, out_ty)
        def sequence(inTensor1, inTensor2, outTensor):
            if trace_size > 0:
                trace_utils.configure_packet_tracing_aie2(
                    tiles_to_trace=tiles_to_trace,
                    shim=ShimTile,
                    trace_size=trace_size,
                )

            in1_task = shim_dma_single_bd_task(
                of_in1, inTensor1, sizes=[1, 1, 1, SEQ_LEN], issue_token=True
            )
            in2_task = shim_dma_single_bd_task(
                of_in2, inTensor2, sizes=[1, 1, 1, SEQ_LEN], issue_token=True
            )
            out_task = shim_dma_single_bd_task(
                of_out, outTensor, sizes=[1, 1, 1, OUT_ELEMS], issue_token=True
            )

            dma_start_task(in1_task, in2_task, out_task)
            dma_await_task(in1_task, in2_task, out_task)

            trace_utils.gen_trace_done_aie2(ShimTile)


p = argparse.ArgumentParser()
p.add_argument("-d", "--dev", required=True, dest="device", help="AIE Device")
p.add_argument(
    "-t",
    "--trace_size",
    required=False,
    dest="trace_size",
    default=0,
    help="Trace buffer size",
)
opts = p.parse_args(sys.argv[1:])

if opts.device == "npu":
    dev = AIEDevice.npu1_1col
elif opts.device == "npu2":
    dev = AIEDevice.npu2
else:
    raise ValueError("[ERROR] Device name {} is unknown".format(opts.device))
trace_size = int(opts.trace_size)

with mlir_mod_ctx() as ctx:
    my_align_kernel(dev, trace_size)
    res = ctx.module.operation.verify()
    if res == True:
        print(ctx.module)
    else:
        print(res)
