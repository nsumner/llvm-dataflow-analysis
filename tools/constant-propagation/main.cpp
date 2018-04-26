
#include "llvm/ADT/APSInt.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
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


static cl::OptionCategory propagationCategory{"constant propagation options"};

static cl::opt<string> inPath{cl::Positional,
                              cl::desc{"<Module to analyze>"},
                              cl::value_desc{"bitcode filename"},
                              cl::init(""),
                              cl::Required,
                              cl::cat{propagationCategory}};


enum class PossibleValueKinds : char {
  UNDEFINED,
  CONSTANT,
  VARIED
};


struct ConstantValue {
  PossibleValueKinds kind;
  llvm::Constant* value;

  ConstantValue()
    : kind{PossibleValueKinds::UNDEFINED},
      value{nullptr}
      { }

  explicit ConstantValue(llvm::Constant* value)
    : kind{PossibleValueKinds::CONSTANT},
      value{value}
      { }

  explicit ConstantValue(PossibleValueKinds kind)
    : kind{kind},
      value{nullptr} {
    assert(kind != PossibleValueKinds::CONSTANT
      && "Constant kind without a value in ConstantValue constructor.");
  }

  ConstantValue
  operator|(const ConstantValue& other) const {
    if (PossibleValueKinds::UNDEFINED == kind) {
      return other;
    } else if ((PossibleValueKinds::UNDEFINED == other.kind) || *this == other) {
      return *this;
    } else {
      return ConstantValue{PossibleValueKinds::VARIED};
    }
  }

  bool
  operator==(const ConstantValue& other) const {
    return kind == other.kind && value == other.value;
  }

  bool
  isConstant() const {
    return kind == PossibleValueKinds::CONSTANT;
  }

  bool
  isVaried() const {
    return kind == PossibleValueKinds::VARIED;
  }
};


using ConstantState  = analysis::AbstractState<ConstantValue>;
using ConstantResult = analysis::DataflowResult<ConstantValue>;


class ConstantMeet : public analysis::Meet<ConstantValue, ConstantMeet> {
public:
  ConstantValue
  meetPair(ConstantValue& s1, ConstantValue& s2) const {
    return s1 | s2;
  }
};


class ConstantTransfer {
  ConstantValue
  getConstantValueFor(llvm::Value* v, ConstantState& state) const {
    if (auto* constant = llvm::dyn_cast<llvm::Constant>(v)) {
      return ConstantValue{constant};
    }
    return state[v];
  }

  ConstantValue
  evaluateBinaryOperator(llvm::BinaryOperator& binOp,
                         ConstantState& state) const {
    auto* op1   = binOp.getOperand(0);
    auto* op2   = binOp.getOperand(1);
    auto value1 = getConstantValueFor(op1, state);
    auto value2 = getConstantValueFor(op2, state);

    if (value1.isConstant() && value2.isConstant()) {
      auto& layout = binOp.getModule()->getDataLayout();
      auto eval    = ConstantFoldBinaryOpOperands(binOp.getOpcode(),
                                            value1.value, value2.value, layout);
      return llvm::isa<llvm::ConstantExpr>(eval)
        ? ConstantValue{PossibleValueKinds::VARIED}
        : ConstantValue{eval};
    } else if (value1.isVaried() || value2.isVaried()) {
      return ConstantValue{PossibleValueKinds::VARIED};
    } else {
      return ConstantValue{PossibleValueKinds::UNDEFINED};
    }
  }

  ConstantValue
  evaluateCast(llvm::CastInst& castOp, ConstantState& state) const {
    auto* op   = castOp.getOperand(0);
    auto value = getConstantValueFor(op, state);

    if (value.isConstant()) {
      auto& layout = castOp.getModule()->getDataLayout();
      auto eval    = ConstantFoldCastOperand(castOp.getOpcode(), value.value,
                                             castOp.getDestTy(), layout);
      return llvm::isa<llvm::ConstantExpr>(eval)
        ? ConstantValue{PossibleValueKinds::VARIED}
        : ConstantValue{eval};
    } else {
      return ConstantValue{value.kind};
    }
  }

public:
  void
  operator()(llvm::Value& i, ConstantState& state) {
    if (auto* constant = llvm::dyn_cast<llvm::Constant>(&i)) {
      state[&i] = ConstantValue{constant};
    } else if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(&i)) {
      state[binOp] = evaluateBinaryOperator(*binOp, state);
    } else if (auto* castOp = llvm::dyn_cast<llvm::CastInst>(&i)) {
      state[castOp] = evaluateCast(*castOp, state);
    } else {
      state[&i].kind = PossibleValueKinds::VARIED;
    }
  }
};


static void
printLineNumber(llvm::raw_ostream& out, llvm::Instruction& inst) {
  if (const llvm::DILocation* debugLoc = inst.getDebugLoc()) {
    out << "At "    << debugLoc->getFilename()
        << " line " << debugLoc->getLine()
        << ":\n";
  } else {
    out << "At an unknown location:\n";
  }  
}


static void
printConstantArguments(ConstantResult& constantStates) {
  for (auto& [value,localState] : constantStates) {
    auto* inst = llvm::dyn_cast<llvm::Instruction>(value);
    if (!inst) {
      continue;
    }

    llvm::CallSite cs{inst};
    if (!cs.getInstruction()) {
      continue;
    }

    auto& state = analysis::getIncomingState(constantStates, *inst);
    bool hasConstantArg = std::any_of(cs.arg_begin(), cs.arg_end(),
      [&state] (auto& use) { return state[use.get()].isConstant(); });
    if (!hasConstantArg) {
      continue;
    }

    llvm::outs() << "CALL ";
    printLineNumber(llvm::outs(), *inst);

    llvm::outs() << cs.getCalledValue()->getName();
    for (auto& use : cs.args()) {
      auto& abstractValue = state[use.get()];
      if (abstractValue.isConstant()) {
        llvm::outs() << " C(" << *abstractValue.value << ")";
      } else {
        llvm::outs() << " XX";
      }
    }
    llvm::outs() << "\n\n";
  }
}


int
main(int argc, char** argv) {
  // This boilerplate provides convenient stack traces and clean LLVM exit
  // handling. It also initializes the built in support for convenient
  // command line option handling.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj shutdown;
  cl::HideUnrelatedOptions(propagationCategory);
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

  using Value    = ConstantValue;
  using Transfer = ConstantTransfer;
  using Meet     = ConstantMeet;
  using Analysis = analysis::DataflowAnalysis<Value, Transfer, Meet>;
  Analysis analysis{*module, mainFunction};
  auto results = analysis.computeDataflow();
  for (auto& [context, contextResults] : results) {
    for (auto& [function, functionResults] : contextResults) {
      printConstantArguments(functionResults);
    }
  }

  return 0;
}
