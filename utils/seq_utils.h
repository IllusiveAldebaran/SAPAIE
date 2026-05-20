#pragma once

#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>

#define MATCH    1
#define MISMATCH 1
#define INS_PENALTY     2
#define DEL_PENALTY     2

constexpr uint16_t sat_sub_u16(uint16_t a, uint16_t b) {
    return (a > b) ? (a - b) : uint16_t(0);
}

inline std::string getCleanedSequence(const std::string& fileName) {
  std::string finalSequence;
  std::ifstream file(fileName);
  if (!file.is_open()) {
    std::cerr << "Could not open file: " << fileName << std::endl;
    return finalSequence;
  }
  std::string line;
  bool headerSkipped = false;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == ';') continue;
    if (!headerSkipped) { headerSkipped = true; continue; }
    for (char c : line) {
      if (std::isspace(c) || c == '1') continue;
      finalSequence += c;
    }
  }
  file.close();
  return finalSequence;
}

/* Smith Waterman Score
 *
 */
inline constexpr uint16_t sw_score(uint8_t* refSeq, uint32_t refInd, uint32_t refLen,
                                   uint8_t* qrySeq, uint32_t qryInd, uint32_t qryLen, 
                                   uint16_t* DP) {
  if (refInd == 0 || qryInd == 0) return 0;

  const size_t DP_COLS = refLen+1;
  const size_t DP_ROWS = qryLen+1;

  uint16_t score_best = 0;
  uint16_t score_diag;
  uint16_t score_horiz;
  uint16_t score_vert;

  // initializing the diag score, not yet deciding whether to match or not
  score_horiz = DP[DP_COLS * qryInd + refInd - 1];
  score_vert  = DP[DP_COLS * (qryInd - 1) + refInd];
  score_diag  = DP[DP_COLS * (qryInd - 1) + refInd - 1];


  // calculate matches and penalties
  if (refSeq[refInd - 1] == qrySeq[qryInd - 1])
    score_diag += MATCH;
  else
    score_diag = sat_sub_u16(score_diag, MISMATCH);

  score_horiz = sat_sub_u16(score_horiz, DEL_PENALTY);
  score_vert  = sat_sub_u16(score_vert, INS_PENALTY);

  score_best = std::max(score_best, score_diag);
  score_best = std::max(score_best, score_horiz);
  score_best = std::max(score_best, score_vert);
  return score_best;
}

inline void fillDPSmithWaterman(uint8_t* refSeq, uint32_t refLen,
                                uint8_t* qrySeq, uint32_t qryLen,
                                uint16_t* DP) {
  const size_t DP_COLS = refLen+1;
  const size_t DP_ROWS = qryLen+1;

  // we initialize with calloc, so no need to set boundaries to 0
  for (size_t j = 1; j <= static_cast<int>(qryLen); j++)
    for (size_t i = 1; i <= static_cast<int>(refLen); i++)
      DP[j * DP_COLS + i] = sw_score(refSeq, i, refLen, qrySeq, j, qryLen, DP);
}

/* Converts from DP regular matrix to DPD filled elements.
 * Just needs to stagger everything
 * Assumes memory is accurately sized
 */
inline void fillDPtoDPD(uint16_t* DP, uint16_t* DPD,
                                const uint32_t DP_COLS, const uint32_t DP_ROWS) {

  for (size_t i = 1; i < DP_COLS; i++)
    for (size_t j = 0; j < DP_ROWS; j++)
      DPD[(j+i)*DP_COLS + i] = DP[j*DP_COLS + i];
}

// prints the alignment DP matrix and sequences
// If diagonally aligned then prints out differently
void showDP(uint8_t* refSeq, uint32_t refLen, uint8_t* qrySeq, uint32_t qryLen, uint16_t* DP, bool diagAligned=false) {
  const size_t DP_COLS = refLen+1;
  const size_t DP_ROWS = qryLen+1;

  printf("Showing DP scores (%dx%d):\n", qryLen, refLen);
  
  printf("        ");
  for(int i = 0; i<refLen; i++){
    printf("   %c", refSeq[i]);
  }
  printf("\n");
  printf("%c  +", (diagAligned)?(char)qrySeq[0] : ' ');
  for(int i = 0; i <= refLen; i++) printf("————");
  printf("\n");
  
  if(!diagAligned) {
    for(int j = 0; j<DP_ROWS; j++) {
      if(j != 0)
        printf(" %c |", qrySeq[j-1]); // we are doing one more than needed
      else
        printf("   |");

      for(int i = 0; i<DP_COLS; i++) {
        printf(" %3d", DP[j * DP_COLS + i]);
      }
      printf("\n");
    }
  } else {
    for(int j = 0; j<DP_ROWS + DP_COLS - 1; j++) {
      if(j == 0)
        printf("%c  |", qrySeq[j+1]);
      else if(j > 0 && j < DP_ROWS-2)
        printf("%c  |", qrySeq[j+1]);
      else
        printf("   |");

      for(int i = 0; i<DP_COLS; i++) {
        printf(" %3d", DP[j * DP_COLS + i]);
      }
      printf("\n");
    }

  }
  

}
