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


static void testParser(string source, string_view expectedStr) {
    SmolParser p;
//    p.addParam(FnParam("x", TypeSet(Value::ANumber), -1));
//    p.addParam(FnParam("y", TypeSet(Value::ANumber), 0));
    source = "(#x y# -- #) " + source;
    try {
        CompiledWord result = p.parse(source);
        string str = disassemble(result);
        cout << source << " \tbecomes:  " << str << endl;
        CHECK(str == expectedStr);
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


TEST_CASE("Pratt Parser") {
    initVocabulary();

    testParser("3+4",            "_LITERAL:<3> _LITERAL:<4> + _DROPARGS<2,1> _RETURN");
    testParser("-(3-4)",         "0 _LITERAL:<3> _LITERAL:<4> - - _DROPARGS<2,1> _RETURN");
    testParser("3+4*5",          "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> * + _DROPARGS<2,1> _RETURN");
    testParser("3*4+5",          "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> + _DROPARGS<2,1> _RETURN");
    testParser("3*(4+5)",        "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> + * _DROPARGS<2,1> _RETURN");
    testParser("3*4 == 5",       "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> = _DROPARGS<2,1> _RETURN");
    testParser(R"("foo"+2)",         R"(_LITERAL:<"foo"> _LITERAL:<2> + _DROPARGS<2,1> _RETURN)");
    testParser(R"("foo\"bar"+2)",    R"(_LITERAL:<"foo\"bar"> _LITERAL:<2> + _DROPARGS<2,1> _RETURN)");

    testParser("3+x",            "_LITERAL:<3> _GETARG<-2> + _DROPARGS<2,1> _RETURN");
    testParser("x+y",            "_GETARG<-1> _GETARG<-1> + _DROPARGS<2,1> _RETURN");
    testParser("12; x",          "_LITERAL:<12> DROP _GETARG<-1> _DROPARGS<2,1> _RETURN");
    testParser("12; x;",         "_LITERAL:<12> DROP _GETARG<-1> _DROPARGS<2,1> _RETURN");
    testParser("x := 5; y",      "_LITERAL:<5> _SETARG<-2> _GETARG<0> _DROPARGS<2,1> _RETURN");
    testParser("x if: 1+2 else: 0", "_GETARG<-1> 0BRANCH<7> _LITERAL:<1> _LITERAL:<2> + "
                                       "BRANCH<2> _LITERAL:<0> _DROPARGS<2,1> _RETURN");
    testParser("let z = 3+4; z", "_LOCALS<1> _LITERAL:<3> _LITERAL:<4> + _SETARG<-1> _GETARG<0> _DROPARGS<3,1> _RETURN");
#if 0
    testParser("x = 3/4",        ":=(x, /(3, 4))");
    testParser("17 if: 1",           "if(17, 1)");
    testParser("[3+4]",              "block(+(3, 4))");
    testParser("[|x| 3+4]",          "block(x, +(3, 4))");
    testParser("[|x foo| 3+4]",      "block(x, foo, +(3, 4))");
    testParser(R"("foo"+2)",         R"(+("foo", 2))");
#endif
}
