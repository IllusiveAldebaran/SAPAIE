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

#include "seq_utils.h"

int main(int argc, char * const argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <reference.seq> <query.seq>\n" << std::endl;
    return 1;
  }

  // File String parsing by Gemini
  std::string refSeqFile = argv[1];
  std::string qrySeqFile = argv[2];


  // parse into seqs into contiguous block of memory and record sizes
  // Also encode sequence
  std::string refParsedSequence = getCleanedSequence(refSeqFile);
  std::string qryParsedSequence = getCleanedSequence(qrySeqFile);

  // Set sequence 1
  uint32_t refLen = refParsedSequence.length();
  uint32_t qryLen = qryParsedSequence.length();

  uint8_t* refSeq = (uint8_t*)calloc(refLen, sizeof(uint8_t));
  uint8_t* qrySeq = (uint8_t*)calloc(qryLen, sizeof(uint8_t));

  for (uint32_t i = 0; i < refLen; i++) {
      refSeq[i] = refParsedSequence[i];
  }
  for (uint32_t i = 0; i < qryLen; i++) {
      qrySeq[i] = qryParsedSequence[i];
  }

  /*
  printf("Reference Sequence:\n");
  for (uint32_t i = 0; i < refLen; i++) {
      printf("%c", refSeq[i]);
  }
  printf("\n");
  printf("Query Sequence:\n");
  for (uint32_t i = 0; i < qryLen; i++) {
      printf("%c", qrySeq[i]);
  }
  printf("\n");
  */

  uint16_t* DP = (uint16_t*)malloc((refLen+1)*(qryLen+1)*sizeof(uint16_t));
  
  // calculate DP matrix and return it
  fillDPSmithWaterman(refSeq, refLen, qrySeq, qryLen, DP);

  showDP(refSeq, refLen, qrySeq, refLen, DP);


  free(refSeq);
  free(qrySeq);
  free(DP);
}
