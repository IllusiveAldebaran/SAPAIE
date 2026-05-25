/* 
 * Authors: Francisco Gutierrez
 * Sequence parser from https://github.com/IllusiveAldebaran/nussinov-gpu
 *
 * Reference Sequence Alignment
 * Runs CPU alignment in typical Smith-Waterman
 * Permutations (diagonal and batching) are only parsed into different buffers after
 *
 * Date of Creation: 3/29/26
 * Date Last Modified: 4/24/26
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

#include <filesystem>


#include "utils/seq_utils.h"
#include "utils/parser.hpp"

int main(int argc, char * const argv[]) {
  struct args myargs = parse_args(argc, (const char **)argv);

  auto pairs = load_pairs(myargs.fasta.c_str());

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
  
  // we can do this because we checked all ref_len are the same size
  // If we want to use different cols then we'll have to check for a max ref_len
  // +1 because we count the padding
  uint32_t batchedDPCols = pairs[0].ref_len + 1;
  uint32_t batchedDPRows = pairs[0].query_len + 1;

  // we reuse refLen and qryLen batching. Perhaps a misnomer, but who cares.
  // We modify our string to have 0s before every sequence
  SeqPair batchedPairs{}; // massive pairs, initialize struct to blanks
  if(myargs.batch) {
    // tries to get filename as name of the batched sequence
    auto p = std::filesystem::path(myargs.fasta);
    if (p.extension() == ".gz") p = p.stem();
    std::string stem = p.stem().string();

    // drops the names of the pairs in favor of just calling it by the file name
    batchedPairs.name = stem;

    for (auto& p : pairs) {
      batchedPairs.ref       += "0" + p.ref;
      batchedPairs.query     += "0" + p.query;
      batchedPairs.ref_len   += p.ref_len + 1; // we add one as we are now counting padding
      batchedPairs.query_len += p.query_len + 1;
    }
  }

  uint16_t* DP  = (uint16_t*)calloc((refLen+1)*(qryLen+1), sizeof(uint16_t));
  uint16_t* DPD;
  uint16_t* DPDB;
  // we use this buffer to generate DPDB
  if(myargs.diagonal || myargs.batch) {
    DPD = (uint16_t*)calloc((refLen+1+qryLen)*(qryLen+1), sizeof(uint16_t));
  }
  if(myargs.batch) {
    // we generate a gigantic sequence as a conctenation of all sequences
    DPDB = (uint16_t*)calloc((batchedPairs.query_len + batchedDPCols - 1)*(batchedDPCols), sizeof(uint16_t));
  }

  // separate from alignment pair struct in case this sequence is transformed or changed
  uint8_t* refSeq = (uint8_t*)calloc(refLen, sizeof(uint8_t));
  uint8_t* qrySeq = (uint8_t*)calloc(qryLen, sizeof(uint8_t));

  {
  uint32_t seqIdx = 0; // pair counter, only used for batching placement
  for (auto& p : pairs) {

    printf("%s", p.toString().c_str());

    memcpy(refSeq, p.ref.c_str(), refLen);
    memcpy(qrySeq, p.query.c_str(), qryLen);

    fillDPSmithWaterman(refSeq, refLen, qrySeq, qryLen, DP);
    if(!myargs.diagonal && !myargs.batch) {
      showDP(refSeq, refLen, qrySeq, qryLen, DP);
    } else {
      fillDPtoDPD(DP, DPD, refLen+1, qryLen+1);
      // only show if we're not batching, else just collect it into the batch buffer
      
      if(!myargs.batch) {
        showDP(refSeq, refLen, qrySeq, qryLen, DPD, true);
      } else {
        // fill up the batch buffer
        fillDPDtoDPDB(DPD, DPDB, refLen+1, qryLen+1, seqIdx);
        seqIdx += qryLen+1; // +1 because extra 0 padding
      }
    }
  }
  }

  if (myargs.batch) {
    printf("%s\n", batchedPairs.toString().c_str());
    showDPDB((uint8_t*)batchedPairs.ref.c_str(), batchedPairs.ref_len, (uint8_t*)batchedPairs.query.c_str(), batchedPairs.query_len, batchedDPRows, batchedDPCols, DPDB);
  }

  free(refSeq);
  free(qrySeq);
  free(DP);

  if(myargs.diagonal || myargs.batch) free(DPD);
  if(myargs.batch) free(DPDB);
}
