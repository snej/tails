//
// tests.cc
//
// 
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "test_utils.hh"


void initVocabulary() {
    static Vocabulary defaultVocab;
    Compiler::activeVocabularies.use(defaultVocab);
    Compiler::activeVocabularies.setCurrent(defaultVocab);
}


Value run(const Word &word, std::initializer_list<Value> inputs) {
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
    Value *sp = call(&stack[inputs.size()] - 1, word.instruction().param.word);
    CHECK(sp == &stack[nOutputs - 1]);
    return *sp;
}


void garbageCollect() {
    Compiler::activeVocabularies.gcScan();
    auto [preserved, freed] = gc::object::sweep();
    cout << "GC: freed " << freed << " objects; " << preserved << " left.\n";
}


//======================== TEST CODE ========================//


#ifdef ENABLE_TRACING
namespace tails {
    /// Tracing function called at the end of each native op -- prints the stack
    void TRACE(Value *sp, const Instruction* pc) {
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


void printStackEffect(StackEffect f) {
    cout << "Stack effect: (" << f << "), max stack " << f.max() << "\n";
}


void printDisassembly(const Word *word) {
    disassemble(cout, *word);
}
