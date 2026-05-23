#include <kseq++/seqio.hpp>
#include <vector>
#include <string>
#include <stdexcept>

#include "seq_utils.h"

using namespace klibpp;

/* Load seq pairs from a fasta file
 */
std::vector<SeqPair> load_pairs(const char* path) {
    std::vector<SeqPair> pairs;
    KSeq rec_ref, rec_query;
    SeqStreamIn iss(path);  // handles .fa, .fasta, .seq, .fa.gz transparently

    while (iss >> rec_ref) {
        if (!(iss >> rec_query))
            throw std::runtime_error("Odd number of sequences — file must have pairs");
        pairs.push_back({
            rec_ref.name + "x" + rec_query.name, // concatenate SeqPair strings names
            rec_ref.seq, rec_query.seq, (uint32_t)rec_ref.seq.length(), (uint32_t)rec_query.seq.length()
        });
    }
    return pairs;
}


/* Function used to get sequence for .seq files
 * Deprecated, since we are always using a pair of sequence
 * we instead now use FASTA files to get sequences pairs
 */
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
