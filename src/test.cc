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


//======================== TOP LEVEL ========================//


/// The data stack. Starts at `DataStack.back()` and grows downwards/backwards.
static std::array<int,1000> DataStack;


/// Top-level function to run a Word.
/// @return  The top value left on the stack.
static int run(const Word &word) {
    assert(!word.isNative()); // must be interpreted
    return * call(DataStack.end(), word._instr.word);
}


/// Top-level function to run an anonymous temporary Word.
/// @return  The top value left on the stack.
static int run(std::initializer_list<CompiledWord::WordRef> words) {
    return run(CompiledWord(words));
}


//======================== TEST CODE ========================//


#ifdef ENABLE_TRACING
    /// Tracing function called at the end of each native op -- prints the stack
    void TRACE(int *sp, const Instruction *pc) {
        printf("\tat %p: ", pc);
        for (auto i = &DataStack.back(); i >= sp; --i)
            printf(" %d", *i);
        putchar('\n');
    }
#endif


static void _test(std::initializer_list<CompiledWord::WordRef> words, const char *sourcecode, int expected) {
    printf("* Testing {%s} ...\n", sourcecode);
    int n = run(words);
    printf("\t-> got %d\n", n);
    assert(n == expected);
}

static void TEST_PARSER(int expected, const char *source) {
    printf("* Parsing “%s”\n", source);
    CompiledWord parsed = CompiledWord::parse(source);
    int n = run(parsed);
    printf("\t-> got %d\n", n);
    assert(n == expected);
}


#define TEST(EXPECTED, ...) _test({__VA_ARGS__}, #__VA_ARGS__, EXPECTED)


int main(int argc, char *argv[]) {
    printf("Known words:");
    for (auto word : Vocabulary::global)
        printf(" %s", word.second->_name);
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

    TEST_PARSER(7, "3 -4 -");
    TEST_PARSER(9604, "4 3 + SQUARE DUP + SQUARE ABS");

    printf("\nTESTS PASSED❣️❣️❣️\n\n");
    return 0;
}
