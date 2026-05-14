//===- conv2dk1_i8.cc -------------------------------------------*- C++ -*-===//

//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2022-2025, Advanced Micro Devices, Inc.
//
//===----------------------------------------------------------------------===//

#define NOCPP

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "aie_kernel_utils.h"
#include <aie_api/aie.hpp>

#define REL_WRITE 0
#define REL_READ 1

#define INS_PENALTY 2 // Gap in Reference (up), subtracts score
#define DEL_PENALTY 2 // Gap in Query (left), subtracts score
#define MISMATCH_SCORE 1 // Mismatch in Nucleotide, subtracts score
#define MATCH_SCORE 1 // Matching Nucleotides Score, adds score

#ifdef ALIGN_SCALAR

constexpr uint16_t sat_sub_u16(uint16_t a, uint16_t b) {
    return (a > b) ? (a - b) : uint16_t(0);
}


const int32_t SMAX = 127;
const int32_t SMIN = 128;
/*****************************************************************************
 * Alignment of 2 sequences of length 16
 * Characters for sequence (unencoded) and uint16_t for scores
 * Note that everything begins at 1 as 0 is the score on the edge
 * EX: with -1 for mismatch scores, match=1, gap=2 (for both)
 *         T   G   A   A   A   T   T   T   T   G   T   T   G   C   A   G
 *         —————————————————————————————————————————————————————————————
 *     0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0
 * T | 0   1   0   0   0   0   1   1   1   1   0   1   1   0   0   0   0
 * G | 0   0   2   0   0   0   0   0   0   0   2   0   0   2   0   0   1
 * A | 0   0   0   3   1   1   0   0   0   0   0   1   0   0   1   1   0
 * C | 0   0   0   1   2   0   0   0   0   0   0   0   0   0   1   0   0
 * T | 0   1   0   0   0   1   1   1   1   1   0   1   1   0   0   0   1
 * T | 0   1   0   0   0   0   2   2   2   2   0   1   2   0   0   0   0
 * T | 0   1   0   0   0   0   1   3   3   3   1   1   2   1   0   0   0
 * G | 0   0   2   0   0   0   0   1   2   2   4   2   0   3   1   0   0
 * C | 0   0   0   1   0   0   0   0   0   1   2   3   1   1   4   2   0
 * T | 0   1   0   0   0   0   1   1   1   1   0   3   4   2   2   3   1
 * A | 0   0   0   1   1   1   0   0   0   0   0   1   2   3   1   3   0
 * T | 0   1   0   0   0   0   2   1   1   1   0   1   2   1   2   1   0
 * G | 0   0   2   0   0   0   0   1   0   0   2   0   0   3   1   1   1
 * C | 0   0   0   1   0   0   0   0   0   0   0   1   0   1   4   2   0
 * A | 0   0   0   1   2   1   0   0   0   0   0   0   0   0   2   5   3
 * G | 0   0   1   0   0   1   0   0   0   0   1   0   0   1   0   3   6
 *
 **************************************************************************/
void align_ch_u16_scalar(char *r_seq, char *q_seq,
                        uint16_t* DPMatrix) {
  event0();
  const size_t RLEN = 16;
  const size_t QLEN = 16;
  // Our DP has padding of 0, so index by one off when getting to DP different from ref and query
  uint16_t (*DP)[RLEN+1] = reinterpret_cast<uint16_t(*)[RLEN+1]>(DPMatrix);

  for(size_t r_ind = 1; r_ind <= RLEN; r_ind++) {
    for(size_t q_ind = 1; q_ind <= QLEN; q_ind++) {
      // calculate DP score
      uint16_t score = 0;

      uint16_t score_ins = sat_sub_u16(DP[q_ind - 1][r_ind], INS_PENALTY);
      uint16_t score_del = sat_sub_u16(DP[q_ind][r_ind - 1], DEL_PENALTY);
      uint16_t score_diag = DP[q_ind - 1][r_ind - 1];

      if(r_seq[r_ind - 1] == q_seq[q_ind - 1]) {
        score_diag = score_diag + MATCH_SCORE;
      } else {
        score_diag = sat_sub_u16(score_diag, MISMATCH_SCORE);
      }
      
      // decide on the score
      score = (score > score_ins ) ? score : score_ins;
      score = (score > score_del ) ? score : score_del;
      score = (score > score_diag) ? score : score_diag;

      // write back
      DP[q_ind][r_ind] = score;
    }
  }

  event1();
}

#else // Vectorized Kernel


//*****************************************************************************
// conv2d 1x1 - vector
// act: int8, wts: int8, out: uint8
//
// Assume IC >= 16 as that gives ideal inner loop schedule
//
// TODO - Restricting input_width is mutiple of 32
// Because each VMAC works on 4 inputs at a time and we store intermediate
// results in 8 accumulators, having input_width be a multiple of 4*8=32 is
// ideal. However, we should be able to support input_width that is only a
// multiple of 4 but there is some strange scheduling happening now so for
// now, we do not.
//*****************************************************************************
void align_u16_vector(int8_t *input, int8_t *kernels, int8_t *output,
                        const int32_t input_width, const int32_t input_channels,
                        const int32_t output_channels, const int scale) {
  event0();

  constexpr int NUM_ACC = 4; // Number of accumulators
  constexpr int MMUL_M = 8;  // Matrix A M size in MxK (Input width)
  constexpr int MMUL_K = 8;
  constexpr int MMUL_N = 8;
  constexpr int CHANNEL_FACTOR = MMUL_K;
  constexpr int MMUL_MK = MMUL_M * MMUL_K;
  constexpr int MMUL_KN = MMUL_K * MMUL_N;
  constexpr int MMUL_MN = MMUL_M * MMUL_N;

  using MMUL8x8x8 = aie::mmul<MMUL_M, MMUL_K, MMUL_N, int8, int8>;
  ::aie::set_saturation(
      aie::saturation_mode::saturate); // Needed to saturate properly to uint8
  ::aie::set_rounding(aie::rounding_mode::symmetric_inf); // Needed to saturate
                                                          // properly to uint8

  int8_t *restrict out_ptr = output;

  const int scaleT = scale;

  MMUL8x8x8 acc_tmp[NUM_ACC];
  for (int x = 0; x < NUM_ACC; x++) {
    acc_tmp[x] = aie::zeros<acc32, MMUL_MN>();
  }

  // TODO Keeping this variable gives a wrong behavior and bad schedule!
  const int iw = input_width;
  const int iw_partial = (input_width / MMUL_M) / NUM_ACC;

  // constexpr int iw_partial_rem = (input_width / MMUL_M) % NUM_ACC;
  // const int iw_partial_rem = (32 / MMUL_M) % NUM_ACC;
  assert((input_width / MMUL_M) % NUM_ACC == 0);
  const int iw_partial_rem = 0; // TODO - See restriction

  assert((input_channels / CHANNEL_FACTOR) > 2); // Assume IC >= 16

  int8_t *input_begin_ptr = input;
  int8_t *input_rem_begin_ptr =
      input + iw_partial * MMUL_M * NUM_ACC * CHANNEL_FACTOR;

  if (iw_partial > 0) {

    for (int oc = 0; oc < (output_channels / CHANNEL_FACTOR); oc++) {
      for (int iw_partialc = 0; iw_partialc < iw_partial; iw_partialc++) {
        AIE_PREPARE_FOR_PIPELINING
        AIE_LOOP_MIN_ITERATION_COUNT(2)
        for (int ic = 0; ic < (input_channels / CHANNEL_FACTOR); ic++) {
          aie::vector<int8, MMUL_KN> in_b = aie::load_v<MMUL_KN>(kernels);
          kernels += MMUL_KN; // wts ic0..7(oc0..7)

          for (int x = 0; x < NUM_ACC; x++) { // 4 acc
            aie::vector<int8, MMUL_MK> in_a = aie::load_v<MMUL_MK>(input);
            input += MMUL_MK; // act oc0..3(ic0..7)
            acc_tmp[x].mac(in_a, in_b);
          }
          // Move to next ic/8 position but in the same input range
          input += (iw * CHANNEL_FACTOR) - MMUL_MK * NUM_ACC;
        }
        // input ptr just moves to next section

        for (int xx = 0; xx < NUM_ACC; xx++) {
          aie::vector<int8, MMUL_MN> o1 = acc_tmp[xx].to_vector<int8>(scaleT);
          aie::store_v(out_ptr, o1);
          out_ptr += MMUL_MN;
          acc_tmp[xx] = aie::zeros<acc32, MMUL_MN>();
        }
        // reset to next set of 64*NUM_ACC inputs
        input -= (input_channels * iw) - MMUL_MK * NUM_ACC;
        // reset kernel back to beginning of ic/8
        kernels -= (input_channels / CHANNEL_FACTOR) * MMUL_KN;
      }
      input = input_begin_ptr; // reset beginning of input ptr
      kernels += (input_channels / CHANNEL_FACTOR) *
                 MMUL_KN; // move to next oc/8 weights
      out_ptr +=
          (iw_partial_rem *
           MMUL_MN); // move to next oc/8 (skip remainder section if present)
    }

  } // if(iw_partial > 0) {

  if (iw_partial_rem > 0) {

    const int ocs = output_channels;
    const int ics = input_channels;

    for (int oc = 0; oc < (ocs / CHANNEL_FACTOR); oc++) {
      AIE_PREPARE_FOR_PIPELINING
      AIE_LOOP_MIN_ITERATION_COUNT(2)
      for (int ic = 0; ic < (ics / CHANNEL_FACTOR); ic++) {
        aie::vector<int8, MMUL_KN> in_b = aie::load_v<MMUL_KN>(kernels);
        kernels += MMUL_KN; // wts ic0..7(oc0..7)

        for (int x = 0; x < iw_partial_rem; x++) {
          aie::vector<int8, MMUL_MK> in_a = aie::load_v<MMUL_MK>(input);
          input += MMUL_MK; // act oc0..3(ic0..7)
          acc_tmp[x].mac(in_a, in_b);
        }
        input += (iw * CHANNEL_FACTOR) -
                 (MMUL_MK * iw_partial_rem); // Move to next ic/8 position
      }
      // input ptr just moves to next section
      for (int xx = 0; xx < iw_partial_rem; xx++) {
        aie::vector<int8, MMUL_MN> o1 = acc_tmp[xx].to_vector<int8>(scaleT);
        aie::store_v(out_ptr, o1);
        out_ptr += MMUL_MN;
        acc_tmp[xx] = aie::zeros<acc32, MMUL_MN>();
      }
      // input   -= ((ics-1)/8)*(iw*8)+(iw_partial_rem*32); // reset to
      // beginning of input ptr for remainder reset to beginning of input ptr
      // for remainder
      input = input_rem_begin_ptr;
      // kernel ptr already at next oc/8
      out_ptr +=
          (iw * CHANNEL_FACTOR) -
          (iw_partial_rem *
           MMUL_MN); // move to next oc/8 (skip remainder section if present)
    }

  } // if(iw_partial_rem > 0)

  event1();
}
#endif

//*****************************************************************************
// conv2d 1x1 wrappers
//*****************************************************************************
extern "C" {

#ifdef ALIGN_SCALAR
void align_ch_u16(char *input, char *kernels, uint16_t *output) {
  align_ch_u16_scalar(input, kernels, output);
}
#else
#error "align_ch_u16 vector implementation not yet implemented — compile with -DALIGN_SCALAR"
#endif // ALIGN_SCALAR

} // extern "C"
