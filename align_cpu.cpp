/* 
 * Authors: Francisco Gutierrez
 * Sequence parser from https://github.com/IllusiveAldebaran/nussinov-gpu
 *
 * Reference Sequence Alignment
 *
 * Date of Creation: 3/29/26
 * Date Last Modified: 3/29/26
 */

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>

#include "utils/seq_utils.h"
#include "utils/parser.hpp"

int main(int argc, char * const argv[]) {
  if (argc != 2) {
      fprintf(stderr, "Usage: %s <file.fa|file.fa.gz>\n", argv[0]);
      return 1;
  }

  auto pairs = load_pairs(argv[1]);

  if (pairs.empty()) {
    fprintf(stderr, "No alignment pairs loaded\n");
    return 1;
  }

  uint32_t refLen = pairs[0].ref_len;
  uint32_t qryLen = pairs[0].query_len;

  // Check that every pair is the same size (for now)
  // TODO: Implement different sizes
  for (auto& p : pairs) {
    if (p.ref_len != refLen || p.query_len != qryLen) {
      fprintf(stderr, "All pairs must be the same size\n");
      return 1;
    }
  }

  uint16_t* DP  = (uint16_t*)calloc((refLen+1)*(qryLen+1), sizeof(uint16_t));
  uint16_t* DPD = (uint16_t*)calloc((refLen+1+qryLen)*(qryLen+1), sizeof(uint16_t));

  // separate from alignment pair struct in case this sequence is transformed or changed
  uint8_t* refSeq = (uint8_t*)calloc(refLen, sizeof(uint8_t));
  uint8_t* qrySeq = (uint8_t*)calloc(qryLen, sizeof(uint8_t));

  for (auto& p : pairs) {
    printf("Alignment Pair: Name:%s Ref:%s Qry:%s Sizes:%zux%zu\n",
           p.name.c_str(),
           p.ref.c_str(),
           p.query.c_str(),
           p.ref.length(),
           p.query.length());

    memcpy(refSeq, p.ref.c_str(), refLen);
    memcpy(qrySeq, p.query.c_str(), qryLen);

    fillDPSmithWaterman(refSeq, refLen, qrySeq, qryLen, DP);
    fillDPtoDPD(DP, DPD, refLen+1, qryLen+1);
    showDP(refSeq, refLen, qrySeq, qryLen, DP);
    showDP(refSeq, refLen, qrySeq, qryLen, DPD, true);
  }

  free(refSeq);
  free(qrySeq);
  free(DP);
  free(DPD);
}
