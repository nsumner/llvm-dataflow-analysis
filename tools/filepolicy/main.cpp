
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <bitset>
#include <memory>
#include <string>

#include "DataflowAnalysis.h"


using namespace llvm;
using std::string;
using std::unique_ptr;


static cl::OptionCategory filePolicyCategory{"file policy options"};

static cl::opt<string> inPath{cl::Positional,
                              cl::desc{"<Module to analyze>"},
                              cl::value_desc{"bitcode filename"},
                              cl::init(""),
                              cl::Required,
                              cl::cat{filePolicyCategory}};


static const llvm::Function *
getCalledFunction(const llvm::CallSite cs) {
  if (!cs.getInstruction()) {
    return nullptr;
  }

  const llvm::Value *called = cs.getCalledValue()->stripPointerCasts();
  return llvm::dyn_cast<llvm::Function>(called);
}


enum PossibleFileValues {
  OPEN,
  CLOSED
};


using FileValue  = std::bitset<2>;
using FileState  = analysis::AbstractState<FileValue>;
using FileResult = analysis::DataflowResult<FileValue>;


class FilePolicyMeet : public analysis::Meet<FileValue, FilePolicyMeet> {
public:
  FileValue
  meetPair(FileValue& s1, FileValue& s2) const {
    return s1 | s2;
  }
};


class FilePolicyTransfer {
public:
  void
  operator()(llvm::Value& v, FileState& state) {
    // Conservatively model all loaded info as unknown
    if (auto* li = dyn_cast<LoadInst>(&v)) {
      state[li].set();
      return;
    }

    const CallSite cs{&v};
    const auto* fun = getCalledFunction(cs);
    // Pretend that indirect calls & non calls don't exist for this analysis
    if (!fun) {
      state[&v].set();
      return;
    }

    // Apply the transfer function to the absract state
    if (fun->getName() == "fopen") {
      auto& value = state[&v];
      value.reset();
      value.set(OPEN);
    } else if (fun->getName() == "fclose") {
      auto *closed = cs.getArgument(0);
      auto& value = state[closed];
      value.reset();
      value.set(CLOSED);
    }
  }
};


static bool
mayBeClosed(FileState& state, Value* arg) {
  const auto found = state.find(arg);
  return state.end() != found && found->second.test(CLOSED);
}


template <typename OutIterator>
static void
collectFileUseBugs(FileResult& fileStates, OutIterator errors) {
  for (auto& valueStatePair : fileStates) {
    auto* inst = llvm::dyn_cast<llvm::Instruction>(valueStatePair.first);
    if (!inst) {
      continue;
    }

    llvm::CallSite cs{inst};
    auto* fun = getCalledFunction(cs);
    if (!fun) {
      continue;
    }

    // Check the incoming state for errors
    auto& state = analysis::getIncomingState(fileStates, *inst);
    if ((fun->getName() == "fread" || fun->getName() == "fwrite")
        && mayBeClosed(state, cs.getArgument(3))) {
      *errors++ = std::make_pair(inst, 3);
      
    } else if ((fun->getName() == "fprintf"
             || fun->getName() == "fflush"
             || fun->getName() == "fclose")
          && mayBeClosed(state, cs.getArgument(0))) {
      *errors++ = std::make_pair(inst, 0);
    }

  }
}


static void
printLineNumber(llvm::raw_ostream& out, llvm::Instruction& inst) {
  if (const llvm::DILocation* debugLoc = inst.getDebugLoc()) {
    out << "At " << debugLoc->getFilename()
        << " line " << debugLoc->getLine()
        << ":\n";
  } else {
    out << "At an unknown location:\n";
  }  
}


static void
printErrors(llvm::ArrayRef<std::pair<llvm::Instruction*, unsigned>> errors) {
  for (auto& errorPair : errors) {
    Instruction* fileOperation;
    unsigned argNum;
    std::tie(fileOperation, argNum) = errorPair;

    llvm::outs().changeColor(raw_ostream::Colors::RED);
    printLineNumber(llvm::outs(), *fileOperation);

    auto* called = getCalledFunction(llvm::CallSite{fileOperation});
    llvm::outs().changeColor(raw_ostream::Colors::YELLOW);
    llvm::outs() << "In call to \"" << called->getName() << "\""
                 << " argument (" << argNum << ") may not be an open file.\n";
  }

  if (errors.empty()) {
    llvm::outs().changeColor(raw_ostream::Colors::GREEN);
    llvm::outs() << "No errors detected\n";
  }
  llvm::outs().resetColor();
}


int
main(int argc, char** argv) {
  // This boilerplate provides convenient stack traces and clean LLVM exit
  // handling. It also initializes the built in support for convenient
  // command line option handling.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj shutdown;
  cl::HideUnrelatedOptions(filePolicyCategory);
  cl::ParseCommandLineOptions(argc, argv);

  // Construct an IR file from the filename passed on the command line.
  SMDiagnostic err;
  LLVMContext context;
  unique_ptr<Module> module = parseIRFile(inPath.getValue(), err, context);

  if (!module.get()) {
    errs() << "Error reading bitcode file: " << inPath << "\n";
    err.print(argv[0], errs());
    return -1;
  }

  auto* mainFunction = module->getFunction("main");
  if (!mainFunction) {
    llvm::report_fatal_error("Unable to find main function.");
  }

  using Value    = FileValue;
  using Transfer = FilePolicyTransfer;
  using Meet     = FilePolicyMeet;
  using Analysis = analysis::ForwardDataflowAnalysis<Value, Transfer, Meet>;
  Analysis analysis{*module, mainFunction};
  auto results = analysis.computeForwardDataflow();

  std::vector<std::pair<llvm::Instruction*, unsigned>> errors;
  for (auto& contextResults : results) {
    for (auto& [function, functionResults] : contextResults.second) {
      collectFileUseBugs(functionResults, std::back_inserter(errors));
    }
  }

  printErrors(errors);

  return 0;
}
