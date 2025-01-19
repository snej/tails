//
// test.cc
//
// Copyright (C) 2021 Jens Alfke. All Rights Reserved.
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

using namespace std;
using namespace tails;

#ifdef ENABLE_TRACING
// Exposed while running, for the TRACE function to use
static Value * StackBase;
#endif


/// Top-level function to run a Word.
/// @return  The top value left on the stack.
static Value run(const Word &word) {
    assert(!word.isNative());           // must be interpreted
    assert(word.stackEffect().inputCount() == 0);  // must not require any inputs
    assert(word.stackEffect().outputCount() > 0);  // must produce results
    size_t stackSize = word.stackEffect().max();
    assert(stackSize >= word.stackEffect().outputCount());
    std::vector<Value> stack;
    stack.resize(stackSize);
    auto stackBase = &stack.front();
#ifdef ENABLE_TRACING
    StackBase = stackBase;
#endif
    return * call(stackBase - 1, word.instruction().word);
}


static void garbageCollect() {
    Compiler::activeVocabularies.gcScan();
    auto [preserved, freed] = gc::object::sweep();
    cout << "GC: freed " << freed << " objects; " << preserved << " left.\n";
}


//======================== TEST CODE ========================//


#ifdef ENABLE_TRACING
    namespace tails {
        /// Tracing function called at the end of each native op -- prints the stack
        void TRACE(Value *sp, const Instruction *pc) {
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


static void printStackEffect(StackEffect f) {
    cout << "Stack effect: (" << f << "), max stack " << f.max() << "\n";
}


static void printDisassembly(const Word *word) {
    auto dis = Disassembler::disassembleWord(word->instruction().word, true);
    for (auto &wordRef : dis) {
        cout << ' ' << (wordRef.word->name() ? wordRef.word->name() : "???");
        if (wordRef.word->hasIntParams())
            cout << "+<" << (int)wordRef.param.offset << '>';
        else if (wordRef.word->hasValParams())
            cout << ":<" << wordRef.param.literal << '>';
        else if (wordRef.word->hasWordParams())
            cout << ":<" << Compiler::activeVocabularies.lookup(wordRef.param.word)->name() << '>';
    }
}


static void _test(std::initializer_list<Compiler::WordRef> words,
                  const char *sourcecode,
                  double expected)
{
    cout << "* Testing {" << sourcecode << "} ...\n";
    CompiledWord word = Compiler::compile(words);
    printStackEffect(word.stackEffect());
    Value result = run(word);
    cout << "\t-> got " << result << "\n";
    assert(result == Value(expected));
}


static Value _runParser(const char *source) {
    cout << "* Parsing “" << source << "”\n";
    Compiler compiler;
    compiler.parse(string(source));
    CompiledWord parsed(std::move(compiler));

    cout << "\tDisassembly:";
    printDisassembly(&parsed);
    cout << "\n";

    printStackEffect(parsed.stackEffect());
    
    Value result = run(parsed);
    cout << "\t-> got " << result << '\n';
    return result;
}


#define TEST(EXPECTED, ...) _test({__VA_ARGS__}, #__VA_ARGS__, EXPECTED)

#define TEST_PARSER(EXPECTED, SRC)  assert(_runParser(SRC) == EXPECTED)


using namespace tails::core_words;


__unused static constexpr StackEffect kSomeTS = "x# -- y#"_sfx;


static void testStackEffect() {
    StackEffect ts = "--"_sfx;
    assert(ts.inputCount() == 0);
    assert(ts.outputCount() == 0);

    ts = "a -- b"_sfx;
    assert(ts.inputCount() == 1);
    assert(ts.outputCount() == 1);
    assert(ts.inputs()[0].flags() == 0x1F);
    assert(ts.outputs()[0].flags() == 0x1F);

    ts = "aaa# bbb#? -- ccc$ [d_d]?"_sfx;
    assert(ts.inputCount() == 2);
    assert(ts.outputCount() == 2);
    assert(ts.inputs()[0].flags() == 0x03);
    assert(ts.inputs()[1].flags() == 0x02);
    assert(ts.outputs()[0].flags() == 0x09);
    assert(ts.outputs()[1].flags() == 0x04);
    assert(!ts.outputs()[0].isInputMatch());
    assert(ts.outputs()[0].inputMatch() == -1);

    ts = "apple ball# cat -- ball# cat apple"_sfx;
    assert(ts.inputCount() == 3);
    assert(ts.outputCount() == 3);
    assert(ts.inputs()[0].flags() == 0x1F);
    assert(ts.inputs()[1].flags() == 0x02);
    assert(ts.inputs()[2].flags() == 0x1F);
    assert(ts.outputs()[0].isInputMatch());
    assert(ts.outputs()[0].inputMatch() == 2);
    assert(ts.outputs()[1].inputMatch() == 0);
    assert(ts.outputs()[2].inputMatch() == 1);
    assert(ts.outputs()[0].flags() == 0x7F);
    assert(ts.outputs()[1].flags() == 0x3F);
    assert(ts.outputs()[2].flags() == 0x42);
}


int main(int argc, char *argv[]) {
    Vocabulary defaultVocab(word::kWords);
    Compiler::activeVocabularies.push(defaultVocab);
    Compiler::activeVocabularies.setCurrent(defaultVocab);

    testStackEffect();

    cout << "Known words:";
    for (auto word : Compiler::activeVocabularies)
        cout << ' ' << word->name();
    cout << "\n";

    garbageCollect();

    TEST(-1234, -1234);
    TEST(-1,    3, 4, MINUS);
    TEST(0.75,  3, 4, DIV);
    TEST(1,     1, 2, 3, ROT);
    TEST(1234,  -1234, ABS);
    TEST(1234,  1234, ABS);
    TEST(4,     3, 4, MAX);
    TEST(4,     4, 3, MAX);

    CompiledWord SQUARE( []() {
        Compiler c("SQUARE");
        c.setStackEffect("# -- #"_sfx);
        c.setInline();
        c.add({DUP});
        c.add({MULT});
        return c;
    }());

    TEST(16,    4, SQUARE);

    TEST(9604,
         4,
         3,
         PLUS,
         SQUARE,
         DUP,
         PLUS,
         SQUARE,
         ABS);

    TEST_PARSER(7,    "3 -4 -");
    TEST_PARSER(14,   "4 3 + DUP + ABS");
    TEST_PARSER(9604, "4 3 + SQUARE DUP + SQUARE ABS");
    TEST_PARSER(2  ,  "2 ABS ABS ABS");                 // testing INTERP2/3/4
    TEST_PARSER(123,  "1 IF 123 ELSE 666 THEN");
    TEST_PARSER(666,  "0 IF 123 ELSE 666 THEN");

    TEST_PARSER(120,  "1 5 begin  dup  while  swap over * swap 1 -  repeat  drop");

    garbageCollect();

    // Strings:
    TEST_PARSER("hello",            R"( "hello" )");
    TEST_PARSER("truthy",           R"( 1 IF "truthy" ELSE "falsey" THEN )");
    TEST_PARSER("HiThere",          R"( "Hi" "There" + )");
    TEST_PARSER(5,                  R"( "hello" LENGTH )");

    // Arrays:
    TEST_PARSER(Value({12,34,56}),  R"( [12 34 56] )");
    TEST_PARSER(Value({Value(12)}), R"( [12] )");
    TEST_PARSER(Value({12,"hi there",Value({}),56}),
                                    R"( [12 "hi there" [] 56] )");
    TEST_PARSER(3,                  R"( [12 34 56] LENGTH )");

    garbageCollect();

    // Quotations and IFELSE:
    TEST_PARSER(3,                  R"( 3 {DUP 4} DROP )");

    TEST_PARSER("yes",              R"( 1 {"yes"} {"no"} IFELSE )");
    TEST_PARSER("no",               R"( 0 {"yes"} {"no"} IFELSE )");
    
    TEST_PARSER(12,                 R"( 3 4  1 {(# # -- #) *} {(# # -- #) +} IFELSE )");
    TEST_PARSER(7,                  R"( 3 4  0 {*} {+} IFELSE )");

    TEST_PARSER(12,                 R"( 3 4  1 {*} {DROP} IFELSE )");
    TEST_PARSER(3,                  R"( 3 4  0 {*} {DROP} IFELSE )");

    // Writing to stdout:
    TEST_PARSER(0,                  R"( "Hello" . SP. 17 . NL. 0 )");

    // Defining a new word:
    TEST_PARSER(0,                  R"( {(# -- #) 3 *} "thrice" define  0 )");
    TEST_PARSER(72,                 R"( 8 thrice Thrice )");

    // Define a typical recursive factorial function:
    TEST_PARSER(0,                  R"( {(# -- #) DUP 1 > IF DUP 1 - RECURSE * ELSE DROP 1 THEN} "factorial" define  0 )");
    TEST_PARSER(120,                R"( 5 factorial )");
    auto fact = Compiler::activeVocabularies.lookup("factorial");
    assert(fact);
    assert(fact->hasFlag(Word::Recursive));

    // Define a tail-recursive form of factorial:
    //   fact(a, n) -> fact(a * n, n - 1)  when n > 1
    //              -> a                   when n ≤ 1
    //           n! -> fact(1, n)
    cout << '\n';
    TEST_PARSER(0,                  R"( {(f# i# -- result#) DUP 1 > IF DUP ROT * SWAP 1 - RECURSE ELSE DROP THEN} "fact" define  0 )");
    fact = Compiler::activeVocabularies.lookup("fact");
    assert(fact);
    cout << "`fact` stack effect: ";
    printStackEffect(fact->stackEffect());
    cout << "`fact` disassembly: ";
    printDisassembly(fact);
    cout << "\n";
    assert(!fact->hasFlag(Word::Recursive));
    assert(fact->stackEffect().max() == 2);

    TEST_PARSER(120,                R"( 1 5 fact )");

    // Define a tail-recursive form of triangle-number:
    cout << '\n';
    TEST_PARSER(0,                  R"( {(f# i# -- result#) DUP 1 > IF DUP ROT + SWAP 1 - RECURSE ELSE DROP THEN} "tri" define  0 )");
    auto tri = Compiler::activeVocabularies.lookup("tri");
    assert(tri);
    cout << "`tri` stack effect: ";
    printStackEffect(tri->stackEffect());
    cout << "`tri` disassembly: ";
    printDisassembly(tri);
    cout << "\n";
    assert(!tri->hasFlag(Word::Recursive));
    assert(tri->stackEffect().max() == 2);

    TEST_PARSER(15,                R"( 1 5 tri )");

#ifndef DEBUG
    auto start = std::chrono::steady_clock::now();
    auto result = _runParser(R"( 1 100000000 tri )");
    assert(result.asDouble() == (1e8 * (1e8 + 1)) / 2);
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = end - start;
    cout << "Got " << result << endl;
    cout << "Time to compute tri(1e8): " << diff.count() << " s; " << (diff.count() / 1e8 * 1e9) << " ns / iteration\n";
#endif

    garbageCollect();
    assert(gc::object::instanceCount() == 0);
    
    cout << "\nTESTS PASSED❣️❣️❣️\n\n";
}
