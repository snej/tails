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
        CompiledWord result = p.parse(source);
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
        for (int i = lhs.outputCount(); i > 0; --i)
            parser.compiler().add(core_words::DROP);
        if (parser.tokens().peek()) {
            auto rhs = parser.nextExpression(1_pri);
            if (rhs.inputCount() > 0)
                throw compile_error("stack underflow, RHS of ';'", parser.tokens().position());
            for (int i = rhs.outputCount(); i > 0; --i)
                parser.compiler().add(core_words::DROP);
            return StackEffect(lhs.inputs(), TypesView{});
        } else {
            return lhs;
        }
    }));

    reg.add(Symbol("else:"));
    reg.add(Symbol("if:")
            .makeInfix(5_pri, 6_pri, [](StackEffect const& lhs, Parser &parser) {
        if (lhs.outputCount() != 1)
            throw compile_error("LHS of 'if:' must have a value", parser.tokens().position());
        auto ifEffect = parser.nextExpression(6_pri);
        if (parser.ifToken("else:")) {
            auto elseEffect = parser.nextExpression(6_pri);
            if (elseEffect.outputs() != ifEffect.outputs())
                throw compile_error("`if` and `else` clauses must return same type", parser.tokens().position());
        } else {
            if (ifEffect.outputCount() != 0)
                throw compile_error("`if` without `else` cannot return a value", parser.tokens().position());
        }
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
                    throw compile_error("Expected a block parameter identifier",
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
                parser.compiler().add(core_words::ZERO);
                auto effect = parser.nextExpression(50_pri);
                if (effect.inputCount() != 0 || effect.outputCount() != 1)
                    throw compile_error("Invalid operand for prefix `-`", parser.tokens().position());
                parser.compiler().add(core_words::MINUS);
                return core_words::ZERO.stackEffect() | effect | core_words::MINUS.stackEffect();
            }));
    reg.add(Symbol(core_words::MULT).makeInfix(60_pri, 61_pri));
    reg.add(Symbol(core_words::DIV)  .makeInfix(60_pri, 61_pri));
//    reg.add(Symbol("self",  Expression::Variable));
//    reg.add(Symbol("x",     Expression::Variable));

    Parser p(reg);

    testParser(p, "3+4",            "_LITERAL:<3> _LITERAL:<4> + _RETURN");
    testParser(p, "-(3-4)",         "0 _LITERAL:<3> _LITERAL:<4> - - _RETURN");
    testParser(p, "3+4*5",          "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> * + _RETURN");
    testParser(p, "3*4+5",          "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> + _RETURN");
    testParser(p, "3*(4+5)",        "_LITERAL:<3> _LITERAL:<4> _LITERAL:<5> + * _RETURN");
    testParser(p, "3*4 == 5",       "_LITERAL:<3> _LITERAL:<4> * _LITERAL:<5> = _RETURN");
#if 0
    testParser(p, "x := 3/4",       ":=(x, /(3, 4))");
    testParser(p, "x = 3/4",        ":=(x, /(3, 4))");
    testParser(p, "12; x",          "none(12, x)");
    testParser(p, "12; x;",         "none(12, x)");
    testParser(p, "17 if: 1",           "if(17, 1)");
    testParser(p, "17 if: 1+2 else: 0", "if(17, +(1, 2), 0)");
    testParser(p, "[3+4]",              "block(+(3, 4))");
    testParser(p, "[|x| 3+4]",          "block(x, +(3, 4))");
    testParser(p, "[|x foo| 3+4]",      "block(x, foo, +(3, 4))");
    testParser(p, R"("foo"+2)",         R"(+("foo", 2))");
    testParser(p, R"("foo\"bar"+2)",    R"(+("foo\"bar", 2))");
#endif
}
