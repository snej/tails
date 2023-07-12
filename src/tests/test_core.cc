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

#include "test_utils.hh"

using namespace tails::core_words;


__unused static constexpr StackEffect kSomeTS = "x# -- y#"_sfx;


TEST_CASE("StackEffect") {
    StackEffect sfx = "--"_sfx;
    CHECK(sfx.inputCount() == 0);
    CHECK(sfx.outputCount() == 0);

    sfx = "a -- b"_sfx;
    CHECK(sfx.inputCount() == 1);
    CHECK(sfx.outputCount() == 1);
    CHECK(sfx.inputs()[0].flags() == 0x1F);
    CHECK(sfx.outputs()[0].flags() == 0x1F);

    sfx = "aaa# bbb#? -- ccc$ [d_d]?"_sfx;
    CHECK(sfx.inputCount() == 2);
    CHECK(sfx.outputCount() == 2);
    CHECK(sfx.inputs()[0].flags() == 0x03);
    CHECK(sfx.inputs()[1].flags() == 0x02);
    CHECK(sfx.outputs()[0].flags() == 0x09);
    CHECK(sfx.outputs()[1].flags() == 0x04);
    CHECK(!sfx.outputs()[0].isInputMatch());
    CHECK(sfx.outputs()[0].inputMatch() == -1);

    sfx = "apple ball# cat -- ball# cat apple"_sfx;
    CHECK(sfx.inputCount() == 3);
    CHECK(sfx.outputCount() == 3);
    CHECK(sfx.inputs()[0].flags() == 0x1F);
    CHECK(sfx.inputs()[1].flags() == 0x02);
    CHECK(sfx.inputs()[2].flags() == 0x1F);
    CHECK(sfx.outputs()[0].isInputMatch());
    CHECK(sfx.outputs()[0].inputMatch() == 2);
    CHECK(sfx.outputs()[1].inputMatch() == 0);
    CHECK(sfx.outputs()[2].inputMatch() == 1);
    CHECK(sfx.outputs()[0].flags() == 0x7F);
    CHECK(sfx.outputs()[1].flags() == 0x3F);
    CHECK(sfx.outputs()[2].flags() == 0x42);

    StackEffectParser sep;
    sep.parse("apple ball# cat -- ball# cat apple");
    CHECK(sep.effect == sfx);
    CHECK(sep.inputNames.size() == 3);
    CHECK(sep.inputNames[0] == "cat");
    CHECK(sep.inputNames[1] == "ball");
    CHECK(sep.inputNames[2] == "apple");
}


TEST_CASE("Vocabulary") {
    initVocabulary();

    int n = 0;
    cout << "Known words:";
    for (auto word : Compiler::activeVocabularies) {
        cout << ' ' << word->name();
        ++n;
    }
    cout << "\n";
    CHECK(n == 46);     // This needs to be updated whenever new core words are added

    for (int i = 0; i < 256 && OpWords[i]; i++) {
        INFO("Checking " << OpWords[i]->name());
        CHECK(OpWords[i]->instruction().opcode == Opcode(i));
    }

    garbageCollect();
}


static inline void _test(std::initializer_list<Compiler::WordRef> words,
                         const char *sourcecode,
                         double expected)
{
    cout << "* Testing {" << sourcecode << "} ...\n";
    CompiledWord word = Compiler::compile(words);
    printStackEffect(word.stackEffect());
    cout << "Disassembly: ";
    printDisassembly(&word);
    cout << endl;
    Value result = run(word);
    cout << "\t-> got " << result << "\n";
    CHECK(result == Value(expected));
}

#define TEST(EXPECTED, ...) _test({__VA_ARGS__}, #__VA_ARGS__, EXPECTED)

TEST_CASE("Compiled Words") {
    initVocabulary();

    TEST(1234,  1234);
    TEST(-1234, -1234);
    TEST(32768, 32768);
    TEST(-32769, -32769);
    TEST(-1,    3, 4, MINUS);
    TEST(0.75,  3, 4, DIV);
    TEST(1,     1, 2, 3, ROT);
    TEST(1234,  -1234, ABS);
    TEST(1234,  1234, ABS);
    TEST(4,     3, 4, MAX);
    TEST(4,     4, 3, MAX);
    
    static CompiledWord SQUARE( []() {
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
}


static inline Value _runParser(const char *source) {
    cout << "* Parsing “" << source << "”\n";
    Compiler compiler;
    compiler.parse(string(source));
    CompiledWord parsed(move(compiler));

    cout << "\tDisassembly:";
    printDisassembly(&parsed);
    cout << "\n";

    printStackEffect(parsed.stackEffect());
    
    Value result = run(parsed);
    cout << "\t-> got " << result << '\n';
    return result;
}

#define TEST_PARSER(EXPECTED, SRC)  CHECK(_runParser(SRC) == EXPECTED)

TEST_CASE("Forth Parser") {
    initVocabulary();

    TEST_PARSER(7,    "3 -4 -");
    TEST_PARSER(14,   "4 3 + DUP + ABS");
    TEST_PARSER(9604, "4 3 + SQUARE DUP + SQUARE ABS");
    TEST_PARSER(2  ,  "2 ABS ABS ABS");                 // testing INTERP2/3/4
    TEST_PARSER(123,  "1 IF 123 ELSE 666 THEN");
    TEST_PARSER(666,  "0 IF 123 ELSE 666 THEN");
    TEST_PARSER(666,  "0 IF \"x\" ELSE 666 THEN");
    
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
}


TEST_CASE("Recursion") {
    // Define a typical recursive factorial function:
    TEST_PARSER(0,                  R"( {(# -- #) DUP 1 > IF DUP 1 - RECURSE * ELSE DROP 1 THEN} "factorial" define  0 )");
    TEST_PARSER(120,                R"( 5 factorial )");
    auto fact = Compiler::activeVocabularies.lookup("factorial");
    CHECK(fact);
    CHECK(fact->hasFlag(Word::Recursive));
    
    // Define a tail-recursive form of factorial:
    //   fact(a, n) -> fact(a * n, n - 1)  when n > 1
    //              -> a                   when n ≤ 1
    //           n! -> fact(1, n)
    cout << '\n';
    TEST_PARSER(0,                  R"( {(f# i# -- result#) DUP 1 > IF DUP ROT * SWAP 1 - RECURSE ELSE DROP THEN} "fact" define  0 )");
    fact = Compiler::activeVocabularies.lookup("fact");
    CHECK(fact);
    cout << "`fact` stack effect: ";
    printStackEffect(fact->stackEffect());
    cout << "`fact` disassembly: ";
    printDisassembly(fact);
    cout << "\n";
    CHECK(!fact->hasFlag(Word::Recursive));
    CHECK(fact->stackEffect().max() == 2);
    
    TEST_PARSER(120,                R"( 1 5 fact )");

    // Define a tail-recursive form of triangle-number:
    cout << '\n';
    TEST_PARSER(0,                  R"( {(f# i# -- result#) DUP 1 > IF DUP ROT + SWAP 1 - RECURSE ELSE DROP THEN} "tri" define  0 )");
    auto tri = Compiler::activeVocabularies.lookup("tri");
    CHECK(tri);
    cout << "`tri` stack effect: ";
    printStackEffect(tri->stackEffect());
    cout << "`tri` disassembly: ";
    printDisassembly(tri);
    cout << "\n";
    CHECK(!tri->hasFlag(Word::Recursive));
    CHECK(tri->stackEffect().max() == 2);

    TEST_PARSER(15,                R"( 1 5 tri )");

#ifndef DEBUG
    auto start = std::chrono::steady_clock::now();
    auto result = _runParser(R"( 1 100000000 tri )");
    CHECK(result.asDouble() == (1e8 * (1e8 + 1)) / 2);
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = end - start;
    cout << "Got " << result << endl;
    cout << "Time to compute tri(1e8): " << diff.count() << " s; " << (diff.count() / 1e8 * 1e9) << " ns / iteration\n";
#endif

    garbageCollect();
    CHECK(gc::object::instanceCount() == 0);
}
