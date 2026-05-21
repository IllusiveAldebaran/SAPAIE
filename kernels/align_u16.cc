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

constexpr uint16_t sat_sub_u16(uint16_t a, uint16_t b) {
    return (a > b) ? (a - b) : uint16_t(0);
}

#ifdef ALIGN_SCALAR

const int32_t SMAX = 127;
const int32_t SMIN = 128;
/*****************************************************************************
 * Alignment of 2 sequences of arbitrary length
 * Characters (uint8_t) for sequence (unencoded) and uint16_t for scores
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
void align_ch_u16_scalar(uint8_t *refSeq, uint32_t refLen, uint8_t *qrySeq, uint32_t qryLen,
                        uint16_t* DP) {
  event0();

  // Our DP has padding of 0, so index by one off when getting to DP different from ref and query
  const size_t DP_COLS = refLen+1;
  const size_t DP_ROWS = qryLen+1;

  for(size_t refInd = 1; refInd < DP_COLS; refInd++) {
    for(size_t qryInd = 1; qryInd < DP_ROWS; qryInd++) {
      // calculate DP score
      uint16_t score = 0;

      uint16_t score_ins = sat_sub_u16(DP[(qryInd - 1)*DP_COLS + refInd], INS_PENALTY);
      uint16_t score_del = sat_sub_u16(DP[qryInd*DP_COLS + (refInd - 1)], DEL_PENALTY);
      uint16_t score_diag = DP[(qryInd - 1)*DP_COLS + (refInd - 1)];

      if(refSeq[refInd - 1] == qrySeq[qryInd - 1]) {
        score_diag = score_diag + MATCH_SCORE;
      } else {
        score_diag = sat_sub_u16(score_diag, MISMATCH_SCORE);
      }
      
      // decide on the score
      score = (score > score_ins ) ? score : score_ins;
      score = (score > score_del ) ? score : score_del;
      score = (score > score_diag) ? score : score_diag;

      // write back
      DP[qryInd*DP_COLS + refInd] = score;
    }
  }

  event1();
}

/* Diagonally prealigned
 * Changes the algorithm in memory accesses, but it is essentially the same.
 */
void alignD_ch_u16_scalar(uint8_t *refSeq, uint32_t refLen, uint8_t *qrySeq, uint32_t qryLen,
                        uint16_t* DP) {

  event0();

  //// Our DP has padding of 0, so index by one off when getting to DP different from ref and query
  const size_t DP_COLS = refLen+1;
  const size_t DP_ROWS = qryLen+1;

  for(size_t dy = 2; dy < DP_ROWS + DP_COLS - 1; dy++) {
    size_t dx_lo = (dy >= DP_ROWS) ? (dy - DP_ROWS + 1) : 1;
    size_t dx_hi = (dy < DP_COLS) ? (dy - 1) : (DP_COLS - 1);

    // iterate through all cols but select to do run the alignment algorithm or not.
    // Makes vectorization (and possibly later on batched alignment) possible
      // check beyond diagonal.
      // Example in (dy, dx) (2, 3) does not exist as its padded area
    for(size_t dx = dx_lo; dx <= dx_hi; dx++) {
      uint16_t score = 0;
      uint16_t score_ins  = sat_sub_u16(DP[(dy-1)*DP_COLS + dx],     INS_PENALTY);
      uint16_t score_del  = sat_sub_u16(DP[(dy-1)*DP_COLS + dx - 1], DEL_PENALTY);
      uint16_t score_diag = DP[(dy-2)*DP_COLS + dx - 1];

      // The exactly diagonal across DP is all 0s or just padded values
      // calculate DP score
      if(refSeq[dx-1] == qrySeq[(dy-dx) - 1])
        score_diag = score_diag + MATCH_SCORE;
      else
        score_diag = sat_sub_u16(score_diag, MISMATCH_SCORE);

      // decide on the score
      score = (score > score_ins)  ? score : score_ins;
      score = (score > score_del)  ? score : score_del;
      score = (score > score_diag) ? score : score_diag;

      // write back
      DP[dy*DP_COLS + dx] = score;
    }
  }

  event1();

}

#else // Vectorized Kernels


/* Diagonally prealigned
 * Changes the algorithm in memory accesses, but it is essentially the same.
 */
void alignD_ch_u16_vector(uint8_t *refSeq, uint32_t refLen, uint8_t *qrySeq, uint32_t qryLen,
                        uint16_t* DP) {
  event0();

  // uint16_t needs min 128 bits = 8 elements on AIE2
  constexpr int VEC = 8;

  const size_t DP_COLS = refLen+1;
  const size_t DP_ROWS = qryLen+1;

  for(size_t dy = 2; dy < DP_ROWS + DP_COLS - 1; dy++) {
    size_t dx_lo = (dy >= DP_ROWS) ? (dy - DP_ROWS + 1) : 1;
    size_t dx_hi = (dy < DP_COLS) ? (dy - 1) : (DP_COLS - 1);

    size_t dx = dx_lo;

    // --- vectorized section (all VEC lanes must be within valid range) ---
    for(; dx + VEC - 1 <= dx_hi; dx += VEC) {
      // gather ref and query chars as uint16_t — uint8_t vectors need 16 elems minimum
      uint16_t refbuf[VEC], qbuf[VEC];
      for(int k = 0; k < VEC; k++) {
        refbuf[k] = refSeq[dx + k - 1];
        qbuf[k]   = qrySeq[(dy - (dx + k)) - 1];
      }
      aie::vector<uint16_t, VEC> vref  = aie::load_v<VEC>(refbuf);
      aie::vector<uint16_t, VEC> vqry  = aie::load_v<VEC>(qbuf);

      aie::vector<uint16_t, VEC> vscore_ins  = aie::load_unaligned_v<VEC>(DP + (dy-1)*DP_COLS + dx);
      aie::vector<uint16_t, VEC> vscore_del  = aie::load_unaligned_v<VEC>(DP + (dy-1)*DP_COLS + dx - 1);
      aie::vector<uint16_t, VEC> vscore_diag = aie::load_unaligned_v<VEC>(DP + (dy-2)*DP_COLS + dx - 1);

      aie::vector<uint16_t, VEC> vINS_PENALTY  = aie::broadcast<uint16_t, VEC>(INS_PENALTY);
      aie::vector<uint16_t, VEC> vMIS_PENALTY  = aie::broadcast<uint16_t, VEC>(MISMATCH_SCORE);
      aie::vector<uint16_t, VEC> vDEL_PENALTY  = aie::broadcast<uint16_t, VEC>(DEL_PENALTY);
      aie::vector<uint16_t, VEC> vMATCH_SCORE  = aie::broadcast<uint16_t, VEC>(MATCH_SCORE);

      // saturating sub: clamp value >= penalty before subtracting so uint never wraps
      vscore_ins = aie::sub(aie::max(vscore_ins, vINS_PENALTY), vINS_PENALTY);
      vscore_del = aie::sub(aie::max(vscore_del, vDEL_PENALTY), vDEL_PENALTY);

      aie::mask<VEC> match_mask = aie::eq(vref, vqry);
      aie::vector<uint16_t, VEC> vdiag_match = aie::add(vscore_diag, vMATCH_SCORE);
      aie::vector<uint16_t, VEC> vdiag_mis   = aie::sub(aie::max(vscore_diag, vMIS_PENALTY), vMIS_PENALTY);
      vscore_diag = aie::select(vdiag_mis, vdiag_match, match_mask);

      aie::vector<uint16_t, VEC> vscore = aie::max(aie::max(vscore_ins, vscore_del), vscore_diag);

      aie::store_unaligned_v(DP + dy*DP_COLS + dx, vscore);
    }

    // --- scalar remainder ---
    for(; dx <= dx_hi; dx++) {
      uint16_t score = 0;
      uint16_t score_ins  = sat_sub_u16(DP[(dy-1)*DP_COLS + dx],     INS_PENALTY);
      uint16_t score_del  = sat_sub_u16(DP[(dy-1)*DP_COLS + dx - 1], DEL_PENALTY);
      uint16_t score_diag = DP[(dy-2)*DP_COLS + dx - 1];

      if(refSeq[dx-1] == qrySeq[(dy-dx) - 1])
        score_diag = score_diag + MATCH_SCORE;
      else
        score_diag = sat_sub_u16(score_diag, MISMATCH_SCORE);

      score = (score > score_ins)  ? score : score_ins;
      score = (score > score_del)  ? score : score_del;
      score = (score > score_diag) ? score : score_diag;
      DP[dy*DP_COLS + dx] = score;
    }
  }

  event1();
}


#endif // ALIGN_SCALAR

//*****************************************************************************
// conv2d 1x1 wrappers
//*****************************************************************************
extern "C" {

void align_ch_u16(uint8_t *refSeq, uint32_t refLen, uint8_t *qrySeq, uint32_t qryLen, uint16_t* DPMatrix){
#ifdef ALIGN_SCALAR
  align_ch_u16_scalar(refSeq, refLen, qrySeq, qryLen, DPMatrix);
#else
// commenting out as we're only using diagonal, but this is still in compilation flow
//#error "align_ch_u16 vector implementation not yet implemented — compile with -DALIGN_SCALAR"
#endif // ALIGN_SCALAR
}

void alignD_ch_u16(uint8_t *refSeq, uint32_t refLen, uint8_t *qrySeq, uint32_t qryLen, uint16_t* DPMatrix){
#ifdef ALIGN_SCALAR
  alignD_ch_u16_scalar(refSeq, refLen, qrySeq, qryLen, DPMatrix);
#else
  alignD_ch_u16_vector(refSeq, refLen, qrySeq, qryLen, DPMatrix);
#endif
}

} // extern "C"
