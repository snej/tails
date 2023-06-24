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

#include "PrattParser.hh"
#include "compiler.hh"
#include "core_words.hh"
#include "io.hh"

using namespace std;
using namespace tails;

void testPrattParser();


static void testParser(Parser &p, string const& source, string_view expectedStr) {
    try {
        StackEffect effect({Value::ANumber, Value::ANumber}, {Value::ANumber});
        CompiledWord result = p.parse(source, effect);
        string str = disassemble(result);
        cout << source << " \tbecomes:  " << str << endl;
        assert(str == expectedStr);
    } catch (compile_error const& x) {
        cout << source << endl;
        if (x.location) {
            auto offset = x.location - &source[0];
            assert(offset >= 0 && offset <= source.size());
            cout << string(offset, ' ') << "^-- ";
        }
        cout << x.what() << endl;
        throw;
    }
}


void testPrattParser() {
    cout << "\n\n-------- PARSER --------\n";
    SymbolRegistry reg;

    // Parentheses:
    reg.add(Symbol(")"));
    reg.add(Symbol("(")
            .makePrefix(5_pri, [](Parser &parser) {
        auto x = parser.nextExpression(5_pri);
        parser.requireToken(")");
        return x;
    }));

    reg.add(Symbol(";")
            .makeInfix(0_pri, 1_pri, [](StackEffect const& lhs, Parser &parser) {
        if (!parser.tokens().peek()) {
            return lhs;
        } else {
            for (int i = lhs.outputCount(); i > 0; --i)
                parser.add(core_words::DROP);
            auto rhs = parser.nextExpression(1_pri);
            if (rhs.inputCount() > 0)
                parser.fail("stack underflow, RHS of ';'");
            return StackEffect(lhs.inputs(), TypesView{});
        }
    }));

    reg.add(Symbol("else:"));
    reg.add(Symbol("if:")
            .makeInfix(5_pri, 6_pri, [](StackEffect const& lhs, Parser &parser) {
        if (lhs.outputCount() != 1)
            parser.fail("LHS of 'if:' must have a value");
        auto instrPos = parser.compiler().add({core_words::_ZBRANCH, intptr_t(-1)},
                                              parser.tokens().position());
        auto ifEffect = parser.nextExpression(6_pri);
        if (parser.ifToken("else:")) {
            auto elsePos = parser.compiler().add({core_words::_BRANCH, intptr_t(-1)},
                                                 parser.tokens().position());
            parser.compiler().fixBranch(instrPos);
            instrPos = elsePos;
            auto elseEffect = parser.nextExpression(6_pri);
            auto outs = elseEffect.outputCount();
            if (outs != ifEffect.outputCount())
                parser.fail("`if` and `else` clauses must return same number of values");
            for (unsigned i = 0; i < outs; ++i)
                ifEffect.outputs()[i] |= elseEffect.outputs()[i];
        } else {
            if (ifEffect.outputCount() != 0)
                parser.fail("`if` without `else` cannot return a value");
        }
        parser.compiler().fixBranch(instrPos);
        return StackEffect(lhs.inputs(), ifEffect.outputs());
    }));


#if 0
    reg.add(Symbol("]"));
    reg.add(Symbol("|"));
    reg.add(Symbol("[")
            .makePrefix(4_pri, [](Parser &parser) {
        vector<Expression> params;
        if (parser.ifToken("|")) {
            while (parser.tokens().peek().literal != "|") {
                auto param = parser.tokens().next();
                if (param.type != Token::Identifier)
                    parser.fail("Expected a block parameter identifier",
                                        parser.tokens().position());
                params.push_back(Expression{
                    .type = Expression::Variable,
                    .identifier = string(param.literal)});
            }
            parser.tokens().consumePeeked();
        }
        params.push_back(parser.nextExpression(5_pri));
        parser.requireToken("]");
        return Expression{.type = Expression::Block, .params = {params}};
    }));
#endif

    reg.add(Symbol(":=")  .makeInfix(11_pri, 10_pri));
    reg.add(Symbol("=")  .makeInfix(21_pri, 20_pri));
    reg.add(Symbol("==")  .makeInfix(21_pri, 20_pri, core_words::EQ));
    reg.add(Symbol(core_words::PLUS)     .makeInfix(50_pri, 51_pri));
    reg.add(Symbol(core_words::MINUS).makeInfix(50_pri, 51_pri)
            .makePrefix(50_pri, [](Parser &parser) {
                parser.add(core_words::ZERO);
                auto effect = parser.nextExpression(50_pri);
                if (effect.inputCount() != 0 || effect.outputCount() != 1)
                    parser.fail("Invalid operand for prefix `-`");
                parser.add(core_words::MINUS);
                return core_words::ZERO.stackEffect() | effect | core_words::MINUS.stackEffect();
            }));
    reg.add(Symbol(core_words::MULT).makeInfix(60_pri, 61_pri));
    reg.add(Symbol(core_words::DIV)  .makeInfix(60_pri, 61_pri));

    SymbolRegistry localReg(&reg);
    localReg.add(FnParam("x", TypeSet(Value::ANumber), -1));
    localReg.add(FnParam("y", TypeSet(Value::ANumber), 0));

    Parser p(localReg);

    testParser(p, "3+4",            "_LITERAL:<3> _LITERAL:<4> + _DROPARGS<2,1> _RETURN");
    testParser(p, "-(3-4)",         "0 _LITERAL:<3> _LITERAL:<4> - - _DROPARGS<2,1> _RETURN");
    testParser(p, "3+4*5",          "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> * + _DROPARGS<2,1> _RETURN");
    testParser(p, "3*4+5",          "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> + _DROPARGS<2,1> _RETURN");
    testParser(p, "3*(4+5)",        "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> + * _DROPARGS<2,1> _RETURN");
    testParser(p, "3*4 == 5",       "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> = _DROPARGS<2,1> _RETURN");
    
    testParser(p, "3+x",            "_LITERAL:<3> _GETARG<-2> + _DROPARGS<2,1> _RETURN");
    testParser(p, "x+y",            "_GETARG<-1> _GETARG<-1> + _DROPARGS<2,1> _RETURN");
    testParser(p, "12; x",          "_LITERAL:<12> DROP _GETARG<-1> _DROPARGS<2,1> _RETURN");
    testParser(p, "12; x;",         "_LITERAL:<12> DROP _GETARG<-1> _DROPARGS<2,1> _RETURN");
    testParser(p, "x := 5; y",      "_LITERAL:<5> _SETARG<-2> _GETARG<0> _DROPARGS<2,1> _RETURN");
    testParser(p, "x if: 1+2 else: 0", "_GETARG<-1> 0BRANCH<7> _LITERAL:<1> _LITERAL:<2> + "
                                       "BRANCH<2> _LITERAL:<0> _DROPARGS<2,1> _RETURN");
#if 0
    testParser(p, "x = 3/4",        ":=(x, /(3, 4))");
    testParser(p, "17 if: 1",           "if(17, 1)");
    testParser(p, "[3+4]",              "block(+(3, 4))");
    testParser(p, "[|x| 3+4]",          "block(x, +(3, 4))");
    testParser(p, "[|x foo| 3+4]",      "block(x, foo, +(3, 4))");
    testParser(p, R"("foo"+2)",         R"(+("foo", 2))");
    testParser(p, R"("foo\"bar"+2)",    R"(+("foo\"bar", 2))");
#endif
}
