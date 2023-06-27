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
static inline Value run(const Word &word) {
    CHECK(!word.isNative());           // must be interpreted
    CHECK(word.stackEffect().inputCount() == 0);  // must not require any inputs
    CHECK(word.stackEffect().outputCount() > 0);  // must produce results
    size_t stackSize = word.stackEffect().max();
    CHECK(stackSize >= word.stackEffect().outputCount());
    std::vector<Value> stack;
    stack.resize(stackSize);
    auto stackBase = &stack.front();
#ifdef ENABLE_TRACING
    StackBase = stackBase;
#endif
    return * call(stackBase - 1, word.instruction().word);
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
            cout << " " << setw(12) << std::left << dis.word->name();
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


