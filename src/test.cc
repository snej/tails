//
// test.cc
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
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
#include "compiler.hh"
#include "vocabulary.hh"
#include <array>


using namespace tails;

#ifdef ENABLE_TRACING
// Exposed while running, for the TRACE function to use
static int * StackTop;
#endif


/// Top-level function to run a Word.
/// @return  The top value left on the stack.
static int run(const Word &word) {
    assert(!word.isNative());           // must be interpreted
    assert(word.stackEffect().input() == 0);  // must not require any inputs
    assert(word.stackEffect().output() > 0);  // must produce results
    size_t stackSize = word.stackEffect().max();
    auto stack = std::vector<int>(stackSize);
    auto stackTop = &stack[stackSize];
#ifdef ENABLE_TRACING
    StackTop = stackTop;
#endif
    return * call(stackTop, word.instruction().word);
}


//======================== TEST CODE ========================//


static_assert( StackEffect(1, 1).then(StackEffect(2,2)) == StackEffect(2, 2));


#ifdef ENABLE_TRACING
    namespace tails {
        /// Tracing function called at the end of each native op -- prints the stack
        void TRACE(int *sp, const Instruction *pc) {
            printf("\tat %p: ", pc);
            for (auto i = StackTop - 1; i >= sp; --i)
                printf(" %d", *i);
            putchar('\n');
        }
    }
#endif


static void printStackEffect(StackEffect f) {
    printf("\t-> stack effect (%d->%d, max %d)\n", f.input(), f.output(), f.max());
}


static void _test(std::initializer_list<CompiledWord::WordRef> words, const char *sourcecode, int expected) {
    printf("* Testing {%s} ...\n", sourcecode);
    CompiledWord word(words);
    printStackEffect(word.stackEffect());
    int n = run(word);
    printf("\t-> got %d\n", n);
    assert(n == expected);
}

static void TEST_PARSER(int expected, const char *source) {
    printf("* Parsing “%s”\n", source);
    CompiledWord parsed = CompiledWord::parse(source);

    printf("\tDisassembly:");
    auto dis = DisassembleWord(parsed.instruction().word);
    for (auto &wordRef : dis) {
        printf(" %s", (wordRef.word.name() ? wordRef.word.name() : "???"));
        if (wordRef.word.hasParam())
            printf("-<%d>", wordRef.param);
    }
    printf("\n");

    printStackEffect(parsed.stackEffect());
    
    int n = run(parsed);
    printf("\t-> got %d\n", n);
    assert(n == expected);
}


#define TEST(EXPECTED, ...) _test({__VA_ARGS__}, #__VA_ARGS__, EXPECTED)


using namespace tails::core_words;


int main(int argc, char *argv[]) {
    printf("Known words:");
    for (auto word : Vocabulary::global)
        printf(" %s", word.second->name());
    printf("\n");

    TEST(-1234, -1234);
    TEST(-1,    3, 4, MINUS);
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

    printf("\nTESTS PASSED❣️❣️❣️\n\n");
    return 0;
}
