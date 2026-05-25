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

struct SeqPair {
  std::string name;
  // note ref and query are parsed as uint8_t almost immediately for any operations
  std::string ref;
  std::string query;
  // these lengths can be retrieved from calculating ref and query lengths, but are just stored for simplicity sake
  uint32_t ref_len;
  uint32_t query_len;

  std::string toString() const {
    return "Alignment Pair -> Name: " + name + " "
          + "Ref: " + ref  + " "
          + "Qry: " + query + " "
          + "Sizes:" + std::to_string(query_len) + "x" + std::to_string(ref_len) + "\n";
  }
};

constexpr uint16_t sat_sub_u16(uint16_t a, uint16_t b) {
    return (a > b) ? (a - b) : uint16_t(0);
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

/* Copies DPD elements to DPDB (diagonal batched)
 * Pretty much 1 to 1 memory location + offset
 */
inline void fillDPDtoDPDB(uint16_t* DPD, uint16_t* DPDB,
                                const uint32_t DP_COLS, const uint32_t DP_ROWS, uint32_t seqIdx) {

  size_t i, j;
  // copy the diagonals. There are DP_COLS diagonals of DP_COLS for every unbatched align
  // aka this traversal is just copying row by row in original DP
  for (size_t d = 0; d < DP_ROWS; d++) {
    for (i = 0; i < DP_COLS; i++) {
      // check that we are not going beyond the diagonal
      j = (d+i+seqIdx);
      DPDB[j*DP_COLS + i] = DPD[(d+i)*DP_COLS + i];
    }
  }
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

/* prints the alignment DP matrix and sequences
 * If diagonally aligned then prints out differently
 * Since alignments are batched all reference sequence and query sequences are packed together
 * Padding is counted as part of the sequence and there is a new variable for the true length of the DP column
 */
void showDPDB(uint8_t* refSeq, uint32_t refLen, uint8_t* qrySeq, uint32_t qryLen, uint32_t bDPRows, uint32_t bDPCols, uint16_t* DP) {
  const size_t DP_COLS = bDPCols;
  const size_t DP_ROWS = qryLen; // this one includes all batched queries here

  printf("Showing multiple DP scores (%dx%d) folded into (%dx%d):\n", bDPRows, refLen, qryLen, bDPCols);
  
  printf("%c\n", qrySeq[0]); // initial pad 0
  printf("%c  +", (char)qrySeq[1]);
  for(int i = 0; i < bDPCols; i++) printf("————");
  printf("\n");
  
  for(int j = 0; j < DP_ROWS + DP_COLS - 1; j++) {
    if(j < DP_ROWS-2)
      printf("%c  |", qrySeq[j+2]);
    else
      printf("   |");

    for(int i = 0; i<DP_COLS; i++) {
      // if padding diagonal then do this, but do not do the last 
      //   or first padding (upper and lower triangular I guess?)
      //   as they are not defined
      if( (j-i) % bDPRows == 0 && (j-i) >= 0 && (j-i) < DP_ROWS) {
        printf("   %c", refSeq[((j-i)/bDPRows)*bDPCols + i]); // (j-i)/bDPRows can tell us which diagonal we are on. beware initial padding.
      } else {
        printf(" %3d", DP[j * DP_COLS + i]);
      }
    }
    printf("\n");
  }
}