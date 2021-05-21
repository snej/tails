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
#include "vocabulary.hh"
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
    assert(word.stackEffect().input() == 0);  // must not require any inputs
    assert(word.stackEffect().output() > 0);  // must produce results
    size_t stackSize = word.stackEffect().max();
    std::vector<Value> stack;
    stack.resize(stackSize);
    auto stackBase = &stack.front();
#ifdef ENABLE_TRACING
    StackBase = stackBase;
#endif
    return * call(stackBase - 1, word.instruction().word);
}


//======================== TEST CODE ========================//


static_assert( StackEffect(1, 1).then(StackEffect(2,2)) == StackEffect(2, 2));


#ifdef ENABLE_TRACING
    namespace tails {
        /// Tracing function called at the end of each native op -- prints the stack
        void TRACE(Value *sp, const Instruction *pc) {
            --pc; // the pc we are passed is of the _next_ Instruction
            cout << "\tafter " << setw(14) << pc;
            if (auto dis = DisassembleInstructionOrParam(pc); dis)
                cout << " " << setw(12) << std::left << dis->word.name();
            cout << ": ";
            for (auto i = StackBase; i <= sp; ++i)
                cout << ' ' << *i;
            cout << '\n';
        }
    }
#endif


static void printStackEffect(StackEffect f) {
    printf("\t-> stack effect (%d->%d, max %d)\n", f.input(), f.output(), f.max());
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
    compiler.parse(string(source), true);
    CompiledWord parsed = compiler.finish();

    cout << "\tDisassembly:";
    auto dis = DisassembleWord(parsed.instruction().word);
    for (auto &wordRef : dis) {
        cout << ' ' << (wordRef.word.name() ? wordRef.word.name() : "???");
        if (wordRef.word.hasIntParam())
            cout << "+<" << (int)wordRef.param.offset << '>';
        else if (wordRef.word.hasValParam())
            cout << ":<" << wordRef.param.literal << '>';
    }
    cout << "\n";

    printStackEffect(parsed.stackEffect());
    
    Value result = run(parsed);
    cout << "\t-> got " << result << '\n';
    return result;
}


#define TEST(EXPECTED, ...) _test({__VA_ARGS__}, #__VA_ARGS__, EXPECTED)

#define TEST_PARSER(EXPECTED, SRC)  assert(_runParser(SRC) == EXPECTED)


using namespace tails::core_words;


int main(int argc, char *argv[]) {
    cout << "Known words:";
    for (auto word : Vocabulary::global)
        cout << ' ' << word.second->name();
    cout << "\n";

    TEST(-1234, -1234);
    TEST(-1,    3, 4, MINUS);
    TEST(0.75,  3, 4, DIV);
    TEST(1,     1, 2, 3, ROT);
    TEST(16,    4, SQUARE);
    TEST(1234,  -1234, ABS);
    TEST(1234,  1234, ABS);
    TEST(4,     3, 4, MAX);
    TEST(4,     4, 3, MAX);

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
    TEST_PARSER(9604, "4 3 + SQUARE DUP + SQUARE ABS");
    TEST_PARSER(10,   "10 20 OVER OVER > 0BRANCH 1 SWAP DROP");
    TEST_PARSER(1,    "53 DUP 13 >= 0BRANCH 5 13 - BRANCH -11");
    TEST_PARSER(123,  "1 IF 123 ELSE 666 THEN");
    TEST_PARSER(666,  "0 IF 123 ELSE 666 THEN");

    TEST_PARSER(120,  "1 5 BEGIN  DUP  WHILE  SWAP OVER * SWAP 1 -  REPEAT  DROP");

#ifndef SIMPLE_VALUE
    TEST_PARSER("hello",            R"( "hello" )");
    TEST_PARSER("truthy",           R"( 1 IF "truthy" ELSE "falsey" THEN )");
    TEST_PARSER("HiThere",          R"( "Hi" "There" + )");
    TEST_PARSER(nullptr,            R"( "Hi" "There" / )");
    TEST_PARSER(5,                  R"( "hello" LENGTH )");

    TEST_PARSER(Value({12,34,56}),  R"( {12 34 56} )");
    TEST_PARSER(Value({Value(12)}), R"( {12} )");
    TEST_PARSER(Value({12,"hi there",Value({}),56}),
                                    R"( {12 "hi there" {} 56} )");
    TEST_PARSER(3,                  R"( {12 34 56} LENGTH )");

    TEST_PARSER(3,                  R"( 3 [DUP 4] DROP)");
#endif
    
    cout << "\nTESTS PASSED❣️❣️❣️\n\n";
}
