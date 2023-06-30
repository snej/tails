//
// tests.hh
//
// 
//

#pragma once

#include "core_words.hh"
#include "compiler.hh"
#include "disassembler.hh"
#include "gc.hh"
#include "more_words.hh"
#include "stack_effect_parser.hh"
#include "vocabulary.hh"
#include "io.hh"
#include <array>
#include <iomanip>
#include <iostream>

#include "catch.hpp"

using namespace std;
using namespace tails;


inline void initVocabulary() {
    static Vocabulary defaultVocab(word::kWords);
    Compiler::activeVocabularies.use(defaultVocab);
    Compiler::activeVocabularies.setCurrent(defaultVocab);
}


#ifdef ENABLE_TRACING
// Exposed while running, for the TRACE function to use
inline Value * StackBase;
#endif


/// Top-level function to run a Word.
/// @return  The top value left on the stack.
static inline Value run(const Word &word, std::initializer_list<Value> inputs = {}) {
    auto nInputs = word.stackEffect().inputCount();
    auto nOutputs = word.stackEffect().outputCount();
    CHECK(!word.isNative());           // must be interpreted
    CHECK(nInputs == inputs.size());
    CHECK(nOutputs > 0);  // must produce results
    size_t stackSize = nInputs + word.stackEffect().max();
    CHECK(stackSize >= word.stackEffect().outputCount());
    std::vector<Value> stack(inputs);
    stack.resize(stackSize);
#ifdef ENABLE_TRACING
    StackBase = &stack[0];
#endif
    Value *sp = call(&stack[inputs.size()] - 1, word.instruction().word);
    CHECK(sp == &stack[nOutputs - 1]);
    return *sp;
}


static inline void garbageCollect() {
    Compiler::activeVocabularies.gcScan();
    auto [preserved, freed] = gc::object::sweep();
    cout << "GC: freed " << freed << " objects; " << preserved << " left.\n";
}


//======================== TEST CODE ========================//


#ifdef ENABLE_TRACING
    namespace tails {
        /// Tracing function called at the end of each native op -- prints the stack
        inline void TRACE(Value *sp, const Instruction *pc) {
            cout << "\tbefore " << setw(14) << pc;
            auto dis = Disassembler::wordOrParamAt(pc);
            cout << " " << setw(15) << std::left << disassemble(dis);
            cout << ": ";
            for (auto i = StackBase; i <= sp; ++i)
                cout << ' ' << *i;
            cout << '\n';
        }
    }
#endif


static inline void printStackEffect(StackEffect f) {
    cout << "Stack effect: (" << f << "), max stack " << f.max() << "\n";
}


static inline void printDisassembly(const Word *word) {
    disassemble(cout, *word);
}


