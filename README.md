# SAPAIE
Sequence Aligment Program for AI Engines (NPU)


# Repo Organization

```
/
├── align_cpu.cpp               # A CPU Version for debugging and comparing
├── parser.cpp                  # Parser for different i/o file formats (Seq and FASTA)
├── sequences/                  # Input files (Sequence and FASTA files)
├── kernels/                    # Collection of relevant NPU kernels and files
│   ├── aie_kernel_utils.h
│   ├── align_u16.cc            # scores kept in unsigned16
├── utils/                      # Set of utils for different parts of the program
│   ├── seq_utils.h             # Preprocessing and Printing (Used by CPU and NPU). And DP scoring (CPU).
│   └── xrt_align_wrapper.h     # Wrapper for NPU initializations
└── tests/                      # Different tests different cores
    ├── seq_input_tests.h       # Collection of sequence pairs to align for testing
    ├── single_core             # Original single core tests
    ├── single_core_diag        # Diagonally organized single core
    └── single_core_diag_batch  # Batched sequences

```

# Naming Convention, Abbreviations and Acronyms

SAPAIE - Sequence Alignment Program for AI Engine
SW - Smith-Waterman Algorithm
SW-Gotoh - Smith-Waterman Algorith with Gotoh variation, for linear penalties
DP - Dynamic Programming. Used for the typical SWA
DPD - Single DP but with diagonal indexing. Padded 0s.
DPDB - DPD but with batched sequences. So DPD are interleaved.


DP* variations are mentioned so to try to draw distinction in the memory addressesing and functions. Sometimes just `DP` is used when they are relatively interchangable.
