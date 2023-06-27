//
// smoltalk.hh
//
// 
//

#pragma once
#include "parser.hh"
#include "compiler.hh"
#include "core_words.hh"
#include "stack_effect_parser.hh"
#include "io.hh"
#include <mutex>

namespace tails {


    class SmolParser : public Parser {
    public:
        explicit SmolParser() :Parser(symbols()) { }

    private:
        virtual StackEffect parseTopLevel() {
            if (ifToken("(")) {
                const char* begin = _tokens.position();
                const char *end = _tokens.skipThrough(')');
                if (!end)
                    fail("Missing ')' to end parameter list");

                StackEffectParser sep;
                setStackEffect(sep.parse(begin, end - 1));
                int i = 0;
                for (auto& name : sep.inputNames) {
                    if (name.empty()) fail("Unnamed parameter");
                    addParam(FnParam(string(name), _effect.inputs()[i], -i));
                    ++i;
                }
            }
            return nextExpression(priority_t::None);
        }

        static SymbolTable const& symbols() {
            std::call_once(sInitOnce, initGrammar);
            return sSymbols;
        }

        static void initGrammar() {
            // Parentheses:
            sSymbols.add(Symbol(")"));
            sSymbols.add(Symbol("(")
                    .makePrefix(5_pri, [](Parser &parser) {
                auto x = parser.nextExpression(5_pri);
                parser.requireToken(")");
                return x;
            }));

            sSymbols.add(Symbol(";")
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

            sSymbols.add(Symbol("else:"));
            sSymbols.add(Symbol("if:")
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

            sSymbols.add(Symbol("let")
                    .makePrefix(5_pri, [](Parser &parser) {
                        // Parse the variable name:
                        Token tok = parser.tokens().next();
                        if (tok.type != Token::Identifier)
                            parser.fail("Expected a local variable name");
                        string name(tok.literal);
                        if (parser.symbols().itselfHas(name))
                            parser.fail(name + " is already a local variable");

                        // Parse the RHS:
                        parser.requireToken("=");
                        StackEffect rhsEffect = parser.nextExpression(1_pri);
                        if (rhsEffect.inputCount() != 0 || rhsEffect.outputCount() != 1)
                            parser.fail("No value to assign to " + name);
                        TypeSet type = rhsEffect.outputs()[0];

                        auto offset = parser.compiler().reserveLocalVariable(type);
                        parser.symbols().add(FnParam(name, type, offset));
                        parser.addSetArg(type, offset);
                        return StackEffect();
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

            sSymbols.add(Symbol(":=")  .makeInfix(11_pri, 10_pri));
            sSymbols.add(Symbol("=")  .makeInfix(21_pri, 20_pri));
            sSymbols.add(Symbol("==")  .makeInfix(21_pri, 20_pri, core_words::EQ));
            sSymbols.add(Symbol(core_words::PLUS)     .makeInfix(50_pri, 51_pri));
            sSymbols.add(Symbol(core_words::MINUS).makeInfix(50_pri, 51_pri)
                    .makePrefix(50_pri, [](Parser &parser) {
                        parser.add(core_words::ZERO);
                        auto effect = parser.nextExpression(50_pri);
                        if (effect.inputCount() != 0 || effect.outputCount() != 1)
                            parser.fail("Invalid operand for prefix `-`");
                        parser.add(core_words::MINUS);
                        return core_words::ZERO.stackEffect() | effect | core_words::MINUS.stackEffect();
                    }));
            sSymbols.add(Symbol(core_words::MULT).makeInfix(60_pri, 61_pri));
            sSymbols.add(Symbol(core_words::DIV)  .makeInfix(60_pri, 61_pri));
        }

    private:
        static inline std::once_flag sInitOnce;
        static inline SymbolTable sSymbols;
    };



}
