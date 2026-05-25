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

// custom wrapper for alignment
#include "../../utils/xrt_align_wrapper.h"
// Set of tests for seq_utils
#include "../../utils/seq_utils.h"
#include <cstdint>
#include <cstring>
#include <sys/stat.h>

//*****************************************************************************
// Modify this section to customize buffer datatypes, initialization functions,
// and verify function. The other place to reconfigure your design is the
// Makefile.
//*****************************************************************************

#ifndef DATATYPES_USING_DEFINED
#define DATATYPES_USING_DEFINED
using DATATYPE_IN  = uint8_t;
using DATATYPE_OUT = uint16_t;
#endif

#include "../seq_input_tests.h"

static const SeqPair *g_test = nullptr;
static std::string g_base_dir; // directory of the executable, for resolving build paths

void initialize_ref(DATATYPE_IN *seq, int seqLen) {
  for (size_t i = 0; i < seqLen; i++)
    seq[i] = (uint8_t)(unsigned char)g_test->ref[i];
  // pad out sequence so it is 4 byte aligned (mlir requirement)
  for (size_t i = seqLen; i < seqLen + (4 - seqLen % 4) % 4; i++)
    seq[i] = (uint8_t)'P';
}

void initialize_qry(DATATYPE_IN *seq, int seqLen) {
  for (size_t i = 0; i < seqLen; i++)
    seq[i] = (uint8_t)(unsigned char)g_test->query[i];
  // pad out sequence so it is 4 byte aligned (mlir requirement)
  for (size_t i = seqLen; i < seqLen + (4 - seqLen % 4) % 4; i++)
    seq[i] = (uint8_t)'P';
}

// Zero output buffer
void initialize_DP(DATATYPE_OUT *bufOut, int SIZE) {
  memset(bufOut, 0, SIZE * sizeof(DATATYPE_OUT));
}

// Verify alignment DP matrix: first row and column must be zero (boundary condition)
int verify_alignment(DATATYPE_IN *refSeq, uint32_t refLen, DATATYPE_IN *qrySeq, uint32_t qryLen,
                     DATATYPE_OUT *DP, int SIZE, int verbosity) {
  const size_t dp_cols = refLen + 1;
  const size_t dp_rows = qryLen + 1;

  int errors = 0;
  // Check boundary row (row 0)
  for (size_t col = 0; col < dp_cols; col++) {
    if (DP[col] != 0) {
      if (verbosity >= 1)
        std::cout << "Boundary error at [0][" << col << "]: got "
                  << DP[col] << " expected 0\n";
      errors++;
    }
  }
  // Check boundary column (col 0 of each row)
  for (size_t row = 0; row < dp_rows + dp_cols - 1; row++) {
    if (DP[row * dp_cols] != 0) {
      if (verbosity >= 1)
        std::cout << "Boundary error at [" << row << "][0]: got "
                  << DP[row * dp_cols] << " expected 0\n";
      errors++;
    }
  }
  if (errors == 0 || verbosity > 0) {
    if(verbosity >= 1)
      printf("Realigning on CPU for comparison\n");
    DATATYPE_OUT *DPCPU = (DATATYPE_OUT *)calloc(dp_cols * dp_rows, sizeof(DATATYPE_OUT));

    // Test on CPU
    fillDPSmithWaterman(refSeq, refLen, qrySeq, qryLen, DPCPU);

    if(verbosity >= 1)
      printf("Verifying Final DP Matrix...\n");
    for (size_t j = 0; j < dp_rows; j++)
      for (size_t i = 0; i < dp_cols; i++)
        if (DPCPU[j * dp_cols + i] != DP[(j+i) * dp_cols + i])
          errors++;

    if (errors != 0) {
      printf("CPU aligned DP\n");
      showDP(refSeq, refLen, qrySeq, qryLen, DPCPU);
    }

    free(DPCPU);
  }

  if (errors != 0) {
    printf("Errors! %d errors accumulated over mismatched DP scores\n", errors);
    showDP(refSeq, refLen, qrySeq, qryLen, DP, true);
  }

  return errors;
}

int runTest(const SeqPair &t, args myargs) {
  g_test = &t;
  const int dp_rows = t.query_len + 1;
  const int dp_cols = t.ref_len + 1;
  // doing aligned kernel dp is based on diagonals
  const int out_elems = (((dp_rows + dp_cols - 1) * dp_cols + 3) / 4) * 4;

  uint32_t REF_VOLUME = t.ref_len;
  uint32_t QRY_VOLUME = t.query_len;
  uint32_t OUT_VOLUME = out_elems;

  // Point to the size-specific build artifacts
  myargs.xclbin = g_base_dir + seq_pair_build_dir(t) + "/final.xclbin";
  myargs.instr  = g_base_dir + seq_pair_build_dir(t) + "/insts.bin";

  std::cout << "=== Test: " << t.name << " ===\n";
  return setup_and_align_aie<DATATYPE_IN, DATATYPE_OUT,
                           initialize_ref, initialize_qry,
                           initialize_DP, verify_alignment>(
      REF_VOLUME, QRY_VOLUME, OUT_VOLUME, myargs);
}


//*****************************************************************************
// Should not need to modify below section
//*****************************************************************************

int main(int argc, const char *argv[]) {
  args myargs = parse_args(argc, argv);

  // Derive base directory from executable path so build/ is always found
  // relative to the test binary location, regardless of cwd.
  std::string exe(argv[0]);
  auto slash = exe.find_last_of('/');
  g_base_dir = (slash != std::string::npos) ? exe.substr(0, slash + 1) : "";

  int return_code = 0;
  constexpr size_t n_groups = sizeof(all_test_groups) / sizeof(all_test_groups[0]);
  for (size_t g = 0; g < n_groups; ++g) {
    std::string dir = g_base_dir + seq_pair_build_dir(all_test_groups[g][0]);
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      if(myargs.verbosity > 1) {
        std::cout << "Skipping " << dir << " (not built)\n";
      }
      continue;
    }
    for (size_t i = 0; i < all_test_group_sizes[g]; ++i)
      return_code |= runTest(all_test_groups[g][i], myargs);
  }

  return return_code;
}
