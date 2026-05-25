TARGET := align_cpu
SRC := align_cpu.cpp
HEADER :=
INCLUDES := -I./kseqpp-1.1.2-noarch/include
LIBS     := -lz
FILELIST = inputs/synthetic-single.fa

# Base Compiler Flags 
CXX := g++
CXXFLAGS := -O3 -std=c++20

# Append all defines to CXXFLAGS
CXXFLAGS += -DSEQ_N=$(SEQ_N)
CXXFLAGS += -DBLOCK_SIZE=$(BLOCK_SIZE)
CXXFLAGS += -DMIN_LOOP_LENGTH=$(MIN_LOOP_LENGTH)
CXXFLAGS += -DGRID_SIZE=$(GRID_SIZE)

# == Build Rules ==
all: $(TARGET)

$(TARGET): $(SRC) $(HEADER)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) -o $@ $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
