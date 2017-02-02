These programs are demonstrations of how LLVM can be used for (very simple)
static intraprocedural dataflow analyses. The presentation is illustrative
and does not demonstrate how to implement scalable analyses.

The provided `filepolicy` analysis identifies simple errors in using fread,
fwrite, and fclose where they may potentially be called on files that have
already been closed.

The provided `constant-propagation` analysis identifies simple constant values
that can be determined at compile time. It then prints out the computable
constant arguments to all function calls in the module.

Building with CMake
==============================================
1. Clone the repository.

        git clone https://github.com/nsumner/llvm-dataflow-analysis.git

2. Create a new directory for building.

        mkdir dfbuild

3. Change into the new directory.

        cd dfbuild

4. Run CMake with the path to the LLVM source.

        cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=True \
            -DLLVM_DIR=</path/to/LLVM/build>/lib/cmake/llvm/ ../llvm-dataflow-analysis

5. Run make inside the build directory:

        make

This produces tools called `bin/filepolicy` and `bin/constant-propagation`.

Note, building with a tool like ninja can be done by adding `-G Ninja` to
the cmake invocation and running ninja instead of make.

Running
==============================================

First suppose that you have a program compiled to bitcode:

    clang -g -c -O1 -emit-llvm ../llvm-dataflow-analysis/test/filepolicy/01straightCorrect.c -o 01.bc

Running the file policy analyzer:

    bin/filepolicy 01.bc

