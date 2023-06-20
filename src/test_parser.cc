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
#include <iostream>
#include <sstream>

using namespace std;
using namespace tails;

void testPrattParser();


static string toString(Expression const& e) {
    std::stringstream s;
    s << e;
    return s.str();
}

static void testParser(Parser &p, string const& source, string_view expectedStr) {
    try {
        Expression result = p.parse(source);
        string str = toString(result);
        cout << source << " \tbecomes:  " << str << endl;
        assert(str == expectedStr);
    } catch (compile_error const& x) {
        cout << source << endl;
        auto offset = x.location - &source[0];
        assert(offset >= 0 && offset <= source.size());
        cout << string(offset, ' ') << "^-- " << x.what() << endl;
        throw;
    }
}


void testPrattParser() {
    cout << "\n\n-------- PARSER --------\n";
    SymbolRegistry reg;

    // Parentheses:
    reg.add(Symbol(")", Expression::None));
    reg.add(Symbol("(", Expression::None) .makePrefix(5_pri, [](Parser &parser) {
        Expression x = parser.nextExpression(5_pri);
        parser.requireToken(")");
        return x;
    }));

    reg.add(Symbol(";", Expression::None) .makeInfix(0_pri, 1_pri, [](Expression &&lhs, Parser &parser) {
        if (parser.tokens().peek()) {
            Expression rhs = parser.nextExpression(1_pri);
            return Expression{.type = Expression::None, .params = {lhs, rhs}};
        } else {
            return lhs;
        }
    }));

    reg.add(Symbol("else:", Expression::None));
    reg.add(Symbol("if:", Expression::If) .makeInfix(5_pri, 6_pri, [](Expression &&lhs, Parser &parser) {
        auto ifClause = parser.nextExpression(6_pri);
        Expression result{.type = Expression::If, .params = {lhs, ifClause}};
        if (parser.ifToken("else:"))
            result.params.push_back( parser.nextExpression(6_pri) );
        return result;
    }));

    reg.add(Symbol("]", Expression::None));
    reg.add(Symbol("|", Expression::None));
    reg.add(Symbol("[", Expression::Block) .makePrefix(4_pri, [](Parser &parser) {
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

    reg.add(Symbol(":=",    Expression::Assign)  .makeInfix(11_pri, 10_pri));
    reg.add(Symbol("=",     Expression::Assign)  .makeInfix(21_pri, 20_pri));
    reg.add(Symbol("==",    Expression::Equals)  .makeInfix(21_pri, 20_pri));
    reg.add(Symbol("+",     Expression::Add)     .makeInfix(50_pri, 51_pri));
    reg.add(Symbol("-",     Expression::Subtract).makeInfix(50_pri, 51_pri).makePrefix(50_pri));
    reg.add(Symbol("*",     Expression::Multiply).makeInfix(60_pri, 61_pri));
    reg.add(Symbol("/",     Expression::Divide)  .makeInfix(60_pri, 61_pri));
    reg.add(Symbol("self",  Expression::Variable));
    reg.add(Symbol("x",     Expression::Variable));

    Parser p(reg);

    testParser(p, "3+4",            "+(3, 4)");
    testParser(p, "-(3-4)",         "-(-(3, 4))");
    testParser(p, "3+4*5",          "+(3, *(4, 5))");
    testParser(p, "3*4+5",          "+(*(3, 4), 5)");
    testParser(p, "3*(4+5)",        "*(3, +(4, 5))");
    testParser(p, "3*4 == 5",       "==(*(3, 4), 5)");
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
}
