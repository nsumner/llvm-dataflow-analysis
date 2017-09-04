
#ifndef DATAFLOW_ANALYSIS_H
#define DATAFLOW_ANALYSIS_H

#include <algorithm>
#include <deque>
#include <numeric>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"


namespace llvm {


template<unsigned long Size>
struct DenseMapInfo<std::array<llvm::Instruction*, Size>> {
  using Context = std::array<llvm::Instruction*, Size>;
  static inline Context
  getEmptyKey() {
    Context c;
    std::fill(c.begin(), c.end(),
      llvm::DenseMapInfo<llvm::Instruction*>::getEmptyKey());
    return c;
  }
  static inline Context
  getTombstoneKey() {
    Context c;
    std::fill(c.begin(), c.end(),
      llvm::DenseMapInfo<llvm::Instruction*>::getTombstoneKey());
    return c;
  }
  static unsigned
  getHashValue(const Context& c) {
    return llvm::hash_combine_range(c.begin(), c.end());
  }
  static bool
  isEqual(const Context& lhs, const Context& rhs) {
    return lhs == rhs;
  }
};


}


namespace analysis {


template<typename T>
class WorkList {
public:
  template<typename IterTy>
  WorkList(IterTy i, IterTy e)
    : inList{},
      work{i, e} {
    inList.insert(i,e);
  }

  WorkList()
    : inList{},
      work{}
      { }

  bool empty() const { return work.empty(); }

  bool contains(T elt) const { return inList.count(elt); }

  void
  add(T elt) {
    if (!inList.count(elt)) {
      work.push_back(elt);
    }
  }

  T
  take() {
    T front = work.front();
    work.pop_front();
    inList.erase(front);
    return front;
  }

private:
  llvm::DenseSet<T> inList;
  std::deque<T> work;
};

using BasicBlockWorklist = WorkList<llvm::BasicBlock*>;


// The dataflow analysis computes three different granularities of results.
// An AbstractValue represents information in the abstract domain for a single
// LLVM Value. An AbstractState is the abstract representation of all values
// relevent to a particular point of the analysis. A DataflowResult contains
// the abstract states before and after each instruction in a function. The
// incoming state for one instruction is the outgoing state from the previous
// instruction. For the first instruction in a BasicBlock, the incoming state is
// keyed upon the BasicBlock itself.
//
// Note: In all cases, the AbstractValue should have a no argument constructor
// that builds constructs the initial value within the abstract domain.

template <typename AbstractValue>
using AbstractState = llvm::DenseMap<llvm::Value*,AbstractValue>;


template <typename AbstractValue>
using DataflowResult =
  llvm::DenseMap<llvm::Value*, AbstractState<AbstractValue>>;


template <typename AbstractValue>
bool
operator==(const AbstractState<AbstractValue>& s1,
           const AbstractState<AbstractValue>& s2) {
  if (s1.size() != s2.size()) {
    return false;
  }
  return std::all_of(s1.begin(), s1.end(),
    [&s2] (auto &kvPair) {
      auto found = s2.find(kvPair.first);
      return found != s2.end() && found->second == kvPair.second;
    });
}


template <typename AbstractValue>
AbstractState<AbstractValue>&
getIncomingState(DataflowResult<AbstractValue>& result, llvm::Instruction& i) {
  auto* bb = i.getParent();
  auto* key = (&bb->front() == &i)
    ? static_cast<llvm::Value*>(bb)
    : static_cast<llvm::Value*>(&*--llvm::BasicBlock::iterator{i});
  return result[key];
}


// NOTE: This class is not intended to be used. It is only intended to
// to document the structure of a Transfer policy object as used by the
// DataflowAnalysis class. For a specific analysis, you should implement
// a class with the same interface.
template <typename AbstractValue>
class Transfer {
public:
  void
  operator()(llvm::Value& v, AbstractState<AbstractValue>& s) {
    llvm_unreachable("unimplemented transfer");
  }
};


// This class can be extended with a concrete implementation of the meet
// operator for two elements of the abstract domain. Implementing the
// `meetPair()` method in the subclass will enable it to be used within the
// general meet operator because of the curiously recurring template pattern.
template <typename AbstractValue, typename SubClass>
class Meet {
public:
  AbstractValue
  operator()(llvm::ArrayRef<AbstractValue> values) {
    return std::accumulate(values.begin(), values.end(),
      AbstractValue(),
      [this] (auto v1, auto v2) {
        return this->asSubClass().meetPair(v1, v2);
      });
  }

  AbstractValue
  meetPair(AbstractValue& v1, AbstractValue& v2) const {
    llvm_unreachable("unimplemented meet");
  }

private:
  SubClass& asSubClass() { return static_cast<SubClass&>(*this); };
};


template <typename AbstractValue,
          typename Transfer,
          typename Meet,
          unsigned long ContextSize=2ul>
class ForwardDataflowAnalysis {
public:
  using State   = AbstractState<AbstractValue>;
  using Context = std::array<llvm::Instruction*, ContextSize>;

  using FunctionResults = DataflowResult<AbstractValue>;
  using ContextFunction = std::pair<Context, llvm::Function*>;
  using ContextResults  = llvm::DenseMap<llvm::Function*, FunctionResults>;
  using ContextWorklist = WorkList<ContextFunction>;

  using ContextMapInfo =
    llvm::DenseMapInfo<std::array<llvm::Instruction*, ContextSize>>;
  using AllResults = llvm::DenseMap<Context, ContextResults, ContextMapInfo>;


  ForwardDataflowAnalysis(llvm::Module& m,
                          llvm::ArrayRef<llvm::Function*> entryPoints) {
    for (auto* entry : entryPoints) {
      contextWork.add({Context{}, entry});
    }
  }


  // computeForwardDataflow collects the dataflow facts for all instructions
  // in the program reachable from the entryPoints passed to the constructor.
  AllResults
  computeForwardDataflow() {
    while (!contextWork.empty()) {
      auto [context, function] = contextWork.take();
      computeForwardDataflow(*function, context);
    }

    return allResults;
  }

  // computeForwardDataflow collects the dataflowfacts for all instructions
  // within Function f with the associated execution context. Functions whose
  // results are required for the analysis of f will be transitively analyzed.
  DataflowResult<AbstractValue>
  computeForwardDataflow(llvm::Function& f, const Context& context) {
    active.insert({context, &f});

    // First compute the initial outgoing state of all instructions
    FunctionResults results = allResults.FindAndConstruct(context).second
                                        .FindAndConstruct(&f).second;
    if (results.find(&f) == results.end()) {
      for (auto& i : llvm::instructions(f)) {
        results.FindAndConstruct(&i);
      }
    }

    // Add all blocks to the worklist in topological order for efficiency
    llvm::ReversePostOrderTraversal<llvm::Function*> rpot(&f);
    BasicBlockWorklist work(rpot.begin(), rpot.end());

    while (!work.empty()) {
      auto* bb = work.take();

      // Save a copy of the outgoing abstract state to check for changes.
      const auto& oldEntryState = results[bb];
      const auto oldExitState   = results[bb->getTerminator()];

      // Merge the state coming in from all predecessors
      auto state = mergeStateFromPredecessors(bb, results);
      mergeInState(state, results[&f]);

      // If we have already processed the block and no changes have been made to
      // the abstract input, we can skip processing the block. Otherwise, save
      // the new entry state and proceed processing this block.
      if (state == oldEntryState && !state.empty()) {
        continue;
      }
      results[bb] = state;

      // Propagate through all instructions in the block
      for (auto& i : *bb) {
        llvm::CallSite cs(&i);
        if (isAnalyzableCall(cs)) {
          analyzeCall(cs, state, context);
        } else {
          applyTransfer(i, state);
        }
        results[&i] = state;
      }

      // If the abstract state for this block did not change, then we are done
      // with this block. Otherwise, we must update the abstract state and
      // consider changes to successors.
      if (state == oldExitState) {
        continue;
      }

      for (auto* s : llvm::successors(bb)) {
        work.add(s);
      }

      if (auto* ret = llvm::dyn_cast<llvm::ReturnInst>(bb->getTerminator())) {
        results[&f][&f] = meet({results[&f][&f], state[ret->getReturnValue()]});
      }
    }

    // The overall results for the given function and context are updated if
    // necessary. Updating the results for this (function,context) means that
    // all callers must be updated as well.
    auto& oldResults = allResults[context][&f];
    if (!(oldResults == results)) {
      oldResults = results;
      for (auto& caller : callers[{context, &f}]) {
        contextWork.add(caller);
      }
    }

    active.erase({context, &f});
    return results;
  }

  llvm::Function*
  getCalledFunction(llvm::CallSite cs) {
    auto* calledValue = cs.getCalledValue()->stripPointerCasts();
    return llvm::dyn_cast<llvm::Function>(calledValue);
  }

  bool
  isAnalyzableCall(llvm::CallSite cs) {
    if (!cs.getInstruction()) {
      return false;
    }
    auto* called = getCalledFunction(cs);
    return called && !called->isDeclaration();
  }

  void
  analyzeCall(llvm::CallSite cs, State &state, const Context& context) {
    Context newContext;
    if (newContext.size() > 0) {
      std::copy(context.begin() + 1, context.end(), newContext.begin());
      newContext.back() = cs.getInstruction();
    }

    auto* caller  = cs.getInstruction()->getFunction();
    auto* callee  = getCalledFunction(cs);
    auto toCall   = std::make_pair(newContext, callee);
    auto toUpdate = std::make_pair(context, caller);

    auto& calledState  = allResults[newContext][callee];
    auto& summaryState = calledState[callee];
    bool needsUpdate   = summaryState.size() == 0;
    unsigned index = 0;
    for (auto& functionArg : callee->args()) {
      auto* passedConcrete = cs.getArgument(index);
      auto passedAbstract = state.find(passedConcrete);
      if (passedAbstract == state.end()) {
        transfer(*passedConcrete, state);
        passedAbstract = state.find(passedConcrete);
      }
      auto& arg     = summaryState[&functionArg];
      auto newState = meet({passedAbstract->second, arg});
      needsUpdate |= !(newState == arg);
      arg = newState;
      ++index;
    }

    if (!active.count(toCall) && needsUpdate) {
      computeForwardDataflow(*callee, newContext);
    }

    state[cs.getInstruction()] = calledState[callee][callee];
    callers[toCall].insert(toUpdate);
  }

private:
  // These property objects determine the behavior of the dataflow analysis.
  // They should by replaced by concrete implementation classes on a per
  // analysis basis.
  Meet meet;
  Transfer transfer;

  AllResults allResults;
  ContextWorklist contextWork;
  llvm::DenseMap<ContextFunction, llvm::DenseSet<ContextFunction>> callers;
  llvm::DenseSet<ContextFunction> active;

  void
  mergeInState(State& destination, const State& toMerge) {
    for (auto& valueStatePair : toMerge) {
      // If an incoming Value has an AbstractValue in the already merged
      // state, meet it with the new one. Otherwise, copy the new value over,
      // implicitly meeting with bottom.
      auto [found, newlyAdded] = destination.insert(valueStatePair);
      if (!newlyAdded) {
        found->second = meet({found->second, valueStatePair.second});
      }
    }
  }

  State
  mergeStateFromPredecessors(llvm::BasicBlock* bb, FunctionResults& results) {
    State mergedState = State{};
    mergeInState(mergedState, results[bb]);
    for (auto* p : llvm::predecessors(bb)) {
      auto predecessorFacts = results.find(p->getTerminator());
      if (results.end() == predecessorFacts) {
        continue;
      }
      mergeInState(mergedState, predecessorFacts->second);
    }
    return mergedState;
  }

  AbstractValue
  meetOverPHI(State& state, const llvm::PHINode& phi) {
    auto phiValue = AbstractValue();
    for (auto& value : phi.incoming_values()) {
      auto found = state.find(value.get());
      if (state.end() == found) {
        transfer(*value.get(), state);
        found = state.find(value.get());
      }
      phiValue = meet({phiValue, found->second});
    }
    return phiValue;
  }

  void
  applyTransfer(llvm::Instruction& i, State& state) {
    // All phis are explicit meet operations
    if (auto* phi = llvm::dyn_cast<llvm::PHINode>(&i)) {
      state[phi] = meetOverPHI(state, *phi);
    } else {
      transfer(i, state);
    }
  }
};


} // end namespace


#endif
