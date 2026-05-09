//
// File originally test.cpp for passthrough_kernel example
// It has since been modified for alignment


//===- test.cpp -------------------------------------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2023, Advanced Micro Devices, Inc.
//
//===----------------------------------------------------------------------===//

#include "xrt_test_wrapper.h"
#include "../../seq_utils.h"
#include <cstdint>
#include <cstring>

//*****************************************************************************
// Modify this section to customize buffer datatypes, initialization functions,
// and verify function. The other place to reconfigure your design is the
// Makefile.
//*****************************************************************************

#ifndef DATATYPES_USING_DEFINED
#define DATATYPES_USING_DEFINED
using DATATYPE_IN1 = std::uint8_t;
using DATATYPE_OUT = std::uint8_t;
#endif

// Initialize input buffer with sequential values
void initialize_bufIn1(DATATYPE_IN1 *bufIn1, int SIZE) {
  for (int i = 0; i < SIZE; i++)
    bufIn1[i] = static_cast<DATATYPE_IN1>(i);
}

// Zero output buffer
void initialize_bufOut(DATATYPE_OUT *bufOut, int SIZE) {
  memset(bufOut, 0, SIZE * sizeof(DATATYPE_OUT));
}

// Passthrough verify: output must equal input byte-for-byte
int verify_passthrough(DATATYPE_IN1 *bufIn1, DATATYPE_OUT *bufOut,
                       int SIZE, int verbosity) {
  int errors = 0;
  for (int i = 0; i < SIZE; i++) {
    if (bufOut[i] != bufIn1[i]) {
      if (verbosity >= 1)
        std::cout << "Error at [" << i << "]: got " << (int)bufOut[i]
                  << " expected " << (int)bufIn1[i] << "\n";
      errors++;
    }
  }
  return errors;
}

//*****************************************************************************
// Should not need to modify below section
//*****************************************************************************

int main(int argc, const char *argv[]) {

  constexpr int IN1_VOLUME = IN1_SIZE / sizeof(DATATYPE_IN1);
  constexpr int OUT_VOLUME = OUT_SIZE / sizeof(DATATYPE_OUT);

  args myargs = parse_args(argc, argv);

  return setup_and_run_aie<DATATYPE_IN1, DATATYPE_OUT,
                           initialize_bufIn1, initialize_bufOut,
                           verify_passthrough>(IN1_VOLUME, OUT_VOLUME, myargs);
}
