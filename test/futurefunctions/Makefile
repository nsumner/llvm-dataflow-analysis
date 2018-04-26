# This makefile can help you to build test inputs for the future functions
# demo.  You must define the CHECKER variable below to contain the path to the
# compiled futurefunctions binary. You may wish to set the paths for CLANG and
# OPT, as well.
#
# To build LLVM assembly files from C source files:
#   make llvmasm
#
# To remove previous output & intermediate files:
#   make clean
#

#LLVM_BIN     := /usr/shared/CMPT/wsumner/llvm/bin/
CHECKER      := ../../../dfbuild/bin/futurefunctions
CLANG        := $(LLVM_BIN)clang
OPT          := $(LLVM_BIN)opt
RM           := rm
SOURCE_FILES := $(sort $(wildcard c/*.c))
ASM_FILES    := $(addprefix ll/,$(notdir $(SOURCE_FILES:.c=.ll)))
TXT_FILES    := $(addprefix txt/,$(notdir $(ASM_FILES:.ll=.txt)))


all: $(TXT_FILES)
llvmasm: $(ASM_FILES)
analyze: ${TXT_FILES}

ll/%.ll: c/%.c
	$(CLANG) -g -emit-llvm -S -Xclang -disable-O0-optnone $< -o - | $(OPT) -mem2reg -S -o $@

txt/%.txt: ll/%.ll
	$(CHECKER) $< | tee $@

clean:
	$(RM) -f txt/*.txt

veryclean:
	$(RM) -f txt/*.txt ll/*.ll

