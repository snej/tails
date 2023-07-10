//
// test_parser.cc
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

#include "smoltalk.hh"
#include "parser.hh"
#include "compiler.hh"
#include "core_words.hh"
#include "io.hh"

#include "tests.hh"


static void testParser(string source,
                       string_view expectedStr,
                       std::initializer_list<Value> inputs,
                       Value expectedOutput)
{
    SmolParser p;
    try {
        cout << "\n## Compiling: " << source << endl;
        INFO("Compiling: " << source);
        CompiledWord result = p.parse(source);
        string str = disassemble(result);
        cout << source << " \tbecomes:  " << str << endl;
        CHECK(str == expectedStr);
        CHECK(run(result, inputs) == expectedOutput);
    } catch (compile_error const& x) {
        cout << source << endl;
        if (x.location) {
            auto offset = x.location - &source[0];
            REQUIRE(offset >= 0);
            REQUIRE(offset <= source.size());
            cout << string(offset, ' ') << "^-- ";
        }
        cout << x.what() << endl;
        throw;
    }
}

static void testParserXY(string source, string_view expectedStr, Value expectedOutput) {
    return testParser("(#x y# -- #) " + source, expectedStr, {7, 8}, expectedOutput);
}


TEST_CASE("Pratt Parser") {
    initVocabulary();

    testParserXY("3+4",
                 "_LITERAL:<3> _LITERAL:<4> + _DROPARGS<2,1> _RETURN",
                 7);
    testParserXY("-(3-4)",
                 "0 _LITERAL:<3> _LITERAL:<4> - - _DROPARGS<2,1> _RETURN",
                 1);
    testParserXY("3+4*5",
                 "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> * + _DROPARGS<2,1> _RETURN",
                 23);
    testParserXY("3*4+5",
                 "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> + _DROPARGS<2,1> _RETURN",
                 17);
    testParserXY("3*(4+5)",
                 "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> + * _DROPARGS<2,1> _RETURN",
                 27);
    testParserXY("3*4 == 5",
                 "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> = _DROPARGS<2,1> _RETURN",
                 0);
    testParserXY(R"("foo" != 2)",
                 R"(_LITERAL:<"foo"> _LITERAL:<2> <> _DROPARGS<2,1> _RETURN)",
                 1);
    testParserXY(R"("foo\"bar" == 2)",
                 R"(_LITERAL:<"foo\"bar"> _LITERAL:<2> = _DROPARGS<2,1> _RETURN)",
                 0);

    testParserXY("3+x",
                 "_LITERAL:<3> _GETARG<-2> + _DROPARGS<2,1> _RETURN",
                 10);
    testParserXY("x+y",
                 "_GETARG<-1> _GETARG<-1> + _DROPARGS<2,1> _RETURN",
                 15);
    testParserXY("12; x",
                 "_LITERAL:<12> DROP _GETARG<-1> _DROPARGS<2,1> _RETURN",
                 7);
    testParserXY("12; x;",
                 "_LITERAL:<12> DROP _GETARG<-1> _DROPARGS<2,1> _RETURN",
                 7);
    testParserXY("x := 5; y",
                 "_LITERAL:<5> _SETARG<-2> _GETARG<0> _DROPARGS<2,1> _RETURN",
                 8);
    testParserXY("x if: 1+2 else: 0",
                 "_GETARG<-1> 0BRANCH<7> _LITERAL:<1> _LITERAL:<2> + BRANCH<2> _LITERAL:<0> _DROPARGS<2,1> _RETURN",
                 3);
    testParserXY("let z = 3+4; z",
                 "_LOCALS<1> _LITERAL:<3> _LITERAL:<4> + _SETARG<-1> _GETARG<0> _DROPARGS<3,1> _RETURN",
                 7);

    testParserXY("abs(-3)",
                 "_LITERAL:<-3> _INTERP:<ABS> _DROPARGS<2,1> _RETURN",
                 3);
    testParserXY("Max(x,y)",
                 "_GETARG<-1> _GETARG<-1> _INTERP:<MAX> _DROPARGS<2,1> _RETURN",
                 8);
    testParserXY("abs(MAX(x,y))",
                 "_GETARG<-1> _GETARG<-1> _INTERP2:<MAX> ABS _DROPARGS<2,1> _RETURN",
                 8);

    testParser("(n# -- #) n > 1 if: recurse(n-1) * n else: n",
               "_GETARG<0> _LITERAL:<1> > 0BRANCH<12> _GETARG<0> _LITERAL:<1> - _RECURSE<-14> _GETARG<-1> * BRANCH<2> _GETARG<0> _DROPARGS<1,1> _RETURN",
               {15},
               Value(1'307'674'368'000));

    testParser("(a# n# -- #) n > 1 if: recurse(a*n, n-1) else: a",
               "_GETARG<0> _LITERAL:<1> > 0BRANCH<14> _GETARG<-1> _GETARG<-1> * _GETARG<-1> _LITERAL:<1> - _RECURSE<-19> BRANCH<2> _GETARG<-1> _DROPARGS<2,1> _RETURN",
               {1, 3},
               Value(6));
    //TODO: Make tail recursion work! The _DROPARGS is preventing it

    testParser("( -- #) let q = {3+4}; q()",
               "_LITERAL:<{( -- #)}> _RETURN",
               {},
               0);
}
