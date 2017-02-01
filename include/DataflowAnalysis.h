
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


namespace analysis {


class WorkList {
  llvm::DenseSet<llvm::BasicBlock*> inList;
  std::deque<llvm::BasicBlock*> work;

public:
  template<typename IterTy>
  WorkList(IterTy i, IterTy e)
    : inList{},
      work{i, e} {
    inList.insert(i,e);
  }

  bool empty() const { return work.empty(); }

  void
  add(llvm::BasicBlock* bb) {
    if (!inList.count(bb)) {
      work.push_back(bb);
    }
  }

  llvm::BasicBlock *
  take() {
    auto* front = work.front();
    work.pop_front();
    inList.erase(front);
    return front;
  }
};

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
  operator()(llvm::Instruction& i, AbstractState<AbstractValue>& s) {
    llvm_unreachable("unimplemented transfer");
  }
};


// This class can be extended with a concrete implementation of the meet
// operator for two elements of the abstract domain. Implementing the
// `meetPair()` method in the subclass will enable it to be used within the
// general meet operator because of the curiously recurring template pattern.
template <typename AbstractValue, typename SubClass>
class Meet {
  SubClass& asSubClass() { return static_cast<SubClass&>(*this); };
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
};


template <typename AbstractValue,
          typename Transfer,
          typename Meet>
class ForwardDataflowAnalysis {
private:
  using State  = AbstractState<AbstractValue>;
  using Result = DataflowResult<AbstractValue>;

  // These property objects determine the behavior of the dataflow analysis.
  // They should by replaced by concrete implementation classes on a per
  // analysis basis.
  Meet meet;
  Transfer transfer;

  State
  mergeStateFromPredecessors(llvm::BasicBlock* bb, Result& results) {
    auto mergedState = State{};
    for (auto* p : llvm::predecessors(bb)) {
      auto predecessorFacts = results.find(p->getTerminator());
      if (results.end() == predecessorFacts) {
        continue;
      }

      auto& toMerge = predecessorFacts->second;
      for (auto& valueStatePair : toMerge) {
        // If an incoming Value has an AbstractValue in the already merged
        // state, meet it with the new one. Otherwise, copy the new value over,
        // implicitly meeting with bottom.
        auto found = mergedState.insert(valueStatePair);
        if (!found.second) {
          found.first->second =
            meet({found.first->second, valueStatePair.second});
        }
      }
    }
    return mergedState;
  }

  AbstractValue
  meetOverPHI(const State& state, const llvm::PHINode& phi) {
    auto phiValue = AbstractValue();
    for (auto& value : phi.incoming_values()) {
      auto found = state.find(value.get());
      if (state.end() != found) {
        phiValue = meet({phiValue, found->second});
      }
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

public:
  DataflowResult<AbstractValue>
  computeForwardDataflow(llvm::Function& f) {
    // First compute the initial outgoing state of all instructions
    Result results;
    for (auto& i : llvm::instructions(f)) {
      results.FindAndConstruct(&i);
    }

    // Add all blocks to the worklist in topological order for efficiency
    llvm::ReversePostOrderTraversal<llvm::Function*> rpot(&f);
    WorkList work(rpot.begin(), rpot.end());

    while (!work.empty()) {
      auto* bb = work.take();

      // Save a copy of the outgoing abstract state to check for changes.
      const auto& oldEntryState = results[bb];
      const auto oldExitState   = results[bb->getTerminator()];

      // Merge the state coming in from all predecessors
      auto state = mergeStateFromPredecessors(bb, results);

      // If we have already processed the block and no changes have been made to
      // the abstract input, we can skip processing the block. Otherwise, save
      // the new entry state and proceed processing this block.
      if (state == oldEntryState && !state.empty()) {
        continue;
      }
      results[bb] = state;

      // Propagate through all instructions in the block
      for (auto& i : *bb) {
        applyTransfer(i, state);
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
    }

    return results;
  }
};


} // end namespace


#endif
