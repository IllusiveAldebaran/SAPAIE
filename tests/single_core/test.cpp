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
using DATATYPE_IN1 = char;
using DATATYPE_IN2 = char;
using DATATYPE_OUT = std::uint16_t;
#endif

constexpr int SEQ_LEN = 16;
constexpr int DP_ROWS = SEQ_LEN + 1; // 17
constexpr int DP_COLS = SEQ_LEN + 1; // 17
// Pad to next multiple of 4 elements for 4-byte DMA alignment (289 -> 292)
constexpr int OUT_ELEMS = ((DP_ROWS * DP_COLS + 3) / 4) * 4; // 292

// Initialize input buffer with 16 characters of sequence data
void initialize_bufIn1(DATATYPE_IN1 *bufIn1, int SIZE) {
  const char *seq = "TGAAATTTTGTTGCAG";
  for (int i = 0; i < SIZE; i++)
    bufIn1[i] = seq[i];
}

void initialize_bufIn2(DATATYPE_IN2 *bufIn2, int SIZE) {
  const char *seq = "TGACTTTGCTATGCAG";
  for (int i = 0; i < SIZE; i++)
    bufIn2[i] = seq[i];
}

// Zero output buffer
void initialize_bufOut(DATATYPE_OUT *bufOut, int SIZE) {
  memset(bufOut, 0, SIZE * sizeof(DATATYPE_OUT));
}

// Verify alignment DP matrix: first row and column must be zero (boundary condition)
int verify_alignment(DATATYPE_IN1 *r_seq, DATATYPE_IN2 *q_seq,
                     DATATYPE_OUT *bufOut, int SIZE, int verbosity) {
  int errors = 0;
  // Check boundary row (row 0)
  for (int col = 0; col < DP_COLS; col++) {
    if (bufOut[col] != 0) {
      if (verbosity >= 1)
        std::cout << "Boundary error at [0][" << col << "]: got "
                  << bufOut[col] << " expected 0\n";
      errors++;
    }
  }
  // Check boundary column (col 0 of each row)
  for (int row = 0; row < DP_ROWS; row++) {
    if (bufOut[row * DP_COLS] != 0) {
      if (verbosity >= 1)
        std::cout << "Boundary error at [" << row << "][0]: got "
                  << bufOut[row * DP_COLS] << " expected 0\n";
      errors++;
    }
  }
  if(errors == 0) {
    printf("Showing DP scores (16x16): \n");

    const size_t RLEN = 16;
    const size_t QLEN = 16;

    DATATYPE_OUT (*DP)[RLEN+1] = reinterpret_cast<DATATYPE_OUT(*)[RLEN+1]>(bufOut);

    printf("        ");
    for(int i = 0; i<=RLEN; i++){
      printf("   %c", r_seq[i]);
    }
    printf("\n");
    printf("   +—————————————————————————————————————————————————————————————————————\n");

    for(int j = 0; j<=QLEN; j++){
      if(j != 0)
        printf(" %c |", q_seq[j-1]); // we are doing one more than needed
      else
        printf("   |");

    for(int i = 0; i<=RLEN; i++) {
        printf(" %3d", bufOut[i*(QLEN+1)+j]);
      }
      printf("\n");
    }
  }
  return errors;
}

//*****************************************************************************
// Should not need to modify below section
//*****************************************************************************

int main(int argc, const char *argv[]) {

  constexpr int IN1_VOLUME = SEQ_LEN / sizeof(DATATYPE_IN1);
  constexpr int IN2_VOLUME = SEQ_LEN / sizeof(DATATYPE_IN2);
  constexpr int OUT_VOLUME = OUT_ELEMS; // 292 (289 valid + 3 padding for DMA alignment)

  args myargs = parse_args(argc, argv);

  return setup_and_run_aie<DATATYPE_IN1, DATATYPE_IN2, DATATYPE_OUT,
                           initialize_bufIn1, initialize_bufIn2,
                           initialize_bufOut, verify_alignment>(
      IN1_VOLUME, IN2_VOLUME, OUT_VOLUME, myargs);
}
