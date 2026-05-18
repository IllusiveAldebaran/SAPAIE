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


def my_align_kernel(dev, trace_size, ref_len, qry_len):
    # DMA BDs require transfer length to be a multiple of 4 bytes (2 uint16 = 4 bytes).
    dp_rows  = qry_len + 1
    dp_cols  = ref_len + 1
    out_elems = ((dp_rows * dp_cols + 1) // 2) * 2  # round up to even number of uint16

    in_dtype  = np.int8   # char encoded nucleotide
    out_dtype = np.uint16

    in1_ty = np.ndarray[(ref_len,),   np.dtype[in_dtype]]
    in2_ty = np.ndarray[(qry_len,),   np.dtype[in_dtype]]
    out_ty = np.ndarray[(out_elems,), np.dtype[out_dtype]]

    rtp_ty = np.ndarray[(1,), np.dtype[np.uint32]]

    @device(dev)
    def device_body():
        # AIE Core Function declarations
        align_u16 = external_func(
            "align_ch_u16",
            inputs=[in1_ty, np.uint32, in2_ty, np.uint32, out_ty],
            link_with="align_u16.cc.o",
        )

        # Tile declarations
        ShimTile    = tile(0, 0)
        ComputeTile2 = tile(0, 2)

        # Set up a packet-switched flow from core to shim for tracing information
        tiles_to_trace = [ComputeTile2, ShimTile]
        if trace_size > 0:
            trace_utils.configure_packet_tracing_flow(tiles_to_trace, ShimTile)

        # RTP buffers for sequence lengths — written by the host at runtime via npu_write_rtp
        rtp_refLen = buffer(ComputeTile2, rtp_ty, "rtp_refLen", use_write_rtp=True)
        rtp_qryLen = buffer(ComputeTile2, rtp_ty, "rtp_qryLen", use_write_rtp=True)

        # AIE-array data movement with object fifos
        # double buffering would be great for multiple alignment kernels
        of_in1 = object_fifo("in1", ShimTile, ComputeTile2, 2, in1_ty)
        of_in2 = object_fifo("in2", ShimTile, ComputeTile2, 2, in2_ty)
        of_out = object_fifo("out", ComputeTile2, ShimTile,  2, out_ty)

        # Compute tile 2
        @core(ComputeTile2)
        def core_body():
            for _ in range_(sys.maxsize):
                elemOut  = of_out.acquire(ObjectFifoPort.Produce, 1)
                elemIn1  = of_in1.acquire(ObjectFifoPort.Consume, 1)
                elemIn2  = of_in2.acquire(ObjectFifoPort.Consume, 1)
                align_u16(elemIn1, rtp_refLen[0], elemIn2, rtp_qryLen[0], elemOut)
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

            # Write sequence lengths into RTP buffers before DMA tasks start
            rtp_refLen[0] = ref_len
            rtp_qryLen[0] = qry_len

            in1_task = shim_dma_single_bd_task(
                of_in1, inTensor1, sizes=[1, 1, 1, ref_len], issue_token=True
            )
            in2_task = shim_dma_single_bd_task(
                of_in2, inTensor2, sizes=[1, 1, 1, qry_len], issue_token=True
            )
            out_task = shim_dma_single_bd_task(
                of_out, outTensor, sizes=[1, 1, 1, out_elems], issue_token=True
            )

            dma_start_task(in1_task, in2_task, out_task)
            dma_await_task(in1_task, in2_task, out_task)

            trace_utils.gen_trace_done_aie2(ShimTile)


p = argparse.ArgumentParser()
p.add_argument("-d", "--dev", required=True, dest="device", help="AIE Device")
p.add_argument(
    "-t", "--trace_size", required=False, dest="trace_size", default=0,
    help="Trace buffer size",
)
p.add_argument(
    "--ref-len", required=False, dest="ref_len", type=int, default=16,
    help="Reference sequence length (baked into MLIR at compile time)",
)
p.add_argument(
    "--qry-len", required=False, dest="qry_len", type=int, default=16,
    help="Query sequence length (baked into MLIR at compile time)",
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
    my_align_kernel(dev, trace_size, opts.ref_len, opts.qry_len)
    res = ctx.module.operation.verify()
    if res == True:
        print(ctx.module)
    else:
        print(res)
