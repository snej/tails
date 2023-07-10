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

    /// A Symbol representing a function parameter or local variable.
    class FnParam : public Symbol {
    public:
        /// Constructs a FnParam.
        /// @param name  Its name
        /// @param type  Its type(s)
        /// @stackPos  Its offset from top of stack at fn entry: 0 = last arg, -1 = prev arg,
        ///            +1 = first local, +2 = 2nd local ...
        FnParam(const string& name, TypeSet type, int stackPos)
        :Symbol(name)
        ,_type(type)
        ,_stackPos(stackPos)
        {
            prefixPriority = 99_pri;
        }

        StackEffect parsePrefix(Parser& parser) const {
            if (parser.ifToken(":=")) {
                // Following token is `:=`, so this is a set:
                StackEffect rhsEffect = parser.nextExpression(10_pri);  //TODO: Don't hardcode
                if (rhsEffect.inputCount() != 0 || rhsEffect.outputCount() != 1)
                    parser.fail("Right-hand side of assignment must have a (single) value");
                else if (rhsEffect.outputs()[0] > _type)
                    parser.fail("Type mismatch assigning to " + token);
                parser.compileSetArg(_type, _stackPos);
                return StackEffect();
            } else {
                // Get arg:
                return parser.compileGetArg(_type, _stackPos);
            }
        }

    private:
        TypeSet _type;
        int _stackPos;
    };


    static void parseParameterList(Parser &parser,
                                   const char *fnName,
                                   unsigned paramCount = UINT_MAX)
    {
        unsigned nArgs = 0;
        while (!parser.ifToken(")")) {
            if (nArgs > paramCount)
                parser.fail(format("Too many arguments; %s expects %d", fnName, paramCount));
            if (nArgs++ > 0)
                parser.requireToken(",");
            auto argEffect = parser.nextExpression(10_pri);
            if (argEffect.inputCount() != 0 || argEffect.outputCount() == 0)
                parser.fail("Invalid function argument");
            else if (argEffect.outputCount() > 1)
                parser.fail("Invalid function argument: multi-valued expression");
        }
        if (nArgs < paramCount && paramCount < UINT_MAX)
            parser.fail(format("Too few arguments; %s expects %d", fnName, paramCount));
    }


    /// A Symbol bound to a function; as a prefix operator it's a function call.
    class FunctionName : public Symbol {
    public:
        explicit FunctionName(Word const& word, std::string name)
                                                :Symbol(word,name) {prefixPriority = 80_pri;}
        explicit FunctionName(Word const& word) :FunctionName(word, word.name()) { }

        StackEffect parsePrefix(Parser& parser) const override {
            StackEffect wordEffect;
            if (word() == core_words::_RECURSE) {
                wordEffect = parser.compiler().stackEffect();
            } else {
                wordEffect = word().stackEffect();
            }
            parser.requireToken("(");
            parseParameterList(parser, token.c_str(), wordEffect.inputCount());
            parser.compileCall(word());
            return StackEffect{{}, wordEffect.outputs()};
        }
    };



    /// Parser for a simple language with infix grammar.
    class SmolParser : public Parser {
    public:
        explicit SmolParser() :Parser(symbols()) { }

        CompiledWord* compileRestOfQuote(SmolParser& topLevelParser) {
            takeTokens(topLevelParser.giveTokens());
            try {
                _compiler = make_unique<Compiler>();
                __unused auto exprEffect = parseTopLevel();
                requireToken("}");
            } catch (...) {
                topLevelParser.takeTokens(giveTokens());
                throw;
            }
            topLevelParser.takeTokens(giveTokens());
            return new CompiledWord(std::move(*_compiler).finish());
        }

    private:
        virtual StackEffect parseTopLevel() override {
//            if (ifToken("def")) {
//                Token tok = tokens().next();
//                if (tok.type != Token::Identifier)
//                    fail("Expected the name of the function to define");
//                string name(tok.literal);
//                if (symbols().has(name))
//                    fail(name + " is already defined");
//                
//                new CompiledWord(*quote, string(name));
//
//            } else {
//                return nextExpression(priority_t::None);
//            }
//        }
//
//        StackEffect parseFunction() {
            if (ifToken("(")) {
                // A '(' at the start of the code delimits the stack effect / parameter list:
                const char* begin = _tokens.position();
                const char *end = _tokens.skipThrough(')');
                if (!end)
                    fail("Missing ')' to end parameter list");

                StackEffectParser sep;
                setStackEffect(sep.parse(begin, end - 1));

                int i = 0;
                for (auto& name : sep.inputNames) {
                    if (name.empty()) fail("Unnamed parameter");
                    _symbols.add(FnParam(string(name), _effect.inputs()[i], -i));
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
            // Arithmetic & comparison operators:
            sSymbols.add(Symbol(core_words::MULT) .makeInfix(60_pri, 61_pri));
            sSymbols.add(Symbol(core_words::DIV)  .makeInfix(60_pri, 61_pri));
            sSymbols.add(Symbol(core_words::PLUS) .makeInfix(50_pri, 51_pri));
            sSymbols.add(Symbol(core_words::MINUS).makeInfix(50_pri, 51_pri)
                                                  .makePrefix(50_pri, &parseUnaryMinus));

            sSymbols.add(Symbol(core_words::LT) .makeInfix(40_pri, 41_pri));
            sSymbols.add(Symbol(core_words::LE) .makeInfix(40_pri, 41_pri));
            sSymbols.add(Symbol(core_words::GT) .makeInfix(40_pri, 41_pri));
            sSymbols.add(Symbol(core_words::GE) .makeInfix(40_pri, 41_pri));

            sSymbols.add(Symbol("==").makeInfix(30_pri, 31_pri, core_words::EQ));
            sSymbols.add(Symbol("!=").makeInfix(30_pri, 31_pri, core_words::NE));
            sSymbols.add(Symbol("≠").makeInfix(30_pri, 31_pri, core_words::NE));

            // Parentheses:
            sSymbols.add(Symbol(")"));
            sSymbols.add(Symbol("(").makePrefix(5_pri, [](Parser &parser) {
                // Prefix parentheses just group a nested expression:
                auto x = parser.nextExpression(5_pri);
                parser.requireToken(")");
                return x;
            }).makePostfix(60_pri, [](StackEffect const& lhs, Parser &parser) {
                // Postfix parentheses are a function call:
                if (lhs.outputCount() != 1 || lhs.outputs()[0] != TypeSet(Value::AQuote))
                    parser.fail("This isn't callable");
                parseParameterList(parser, "a quote");
                parser.compileCall(core_words::CALL);
                return " -- ?"_sfx; //FIXME: We don't know what the quote really returns
            }));

            // Curly braces define a quote/block:
            sSymbols.add(Symbol("}"));
            sSymbols.add(Symbol("{").makePrefix(2_pri, [](Parser &parser) {
                SmolParser quoteParser;
                CompiledWord* quote = quoteParser.compileRestOfQuote(dynamic_cast<SmolParser&>(parser));
                parser.compileLiteral(Value(quote));
                StackEffect effect;
               // effect.addOutput(TypeSet::quoteWithEffect(quote->stackEffect()));
                return effect;
            }));

            // ';' separates expressions. All but the last have their output values dropped.
            sSymbols.add(Symbol(";")
                    .makeInfix(3_pri, 4_pri, [](StackEffect const& lhs, Parser &parser) {
                if (!parser.tokens().peek()) {
                    return lhs; // Allow a trailing `;` as a no-op
                } else {
                    for (int i = lhs.outputCount(); i > 0; --i)
                        parser.compileCall(core_words::DROP);   // restore stack
                    auto rhs = parser.nextExpression(1_pri);
                    if (rhs.inputCount() > 0)
                        parser.fail("stack underflow, RHS of ';'");
                    return StackEffect(lhs.inputs(), TypesView{});
                }
            }));

            // `<cond> if: <expr> else: <expr>`
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

            // Assignment
            sSymbols.add(Symbol(":=")  .makeInfix(11_pri, 10_pri));
            sSymbols.add(Symbol("=")  .makeInfix(21_pri, 20_pri));

            // `let <var> = <value>` -- defines a new local variable
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
                        StackEffect rhsEffect = parser.nextExpression(5_pri);
                        if (rhsEffect.inputCount() != 0 || rhsEffect.outputCount() != 1)
                            parser.fail("No value to assign to " + name);
                        TypeSet type = rhsEffect.outputs()[0];

                        auto offset = parser.compiler().reserveLocalVariable(type);
                        parser.symbols().add(FnParam(name, type, offset));
                        parser.compileSetArg(type, offset);
                        return StackEffect();
            }));

            // RECURSE calls this function recursively
            sSymbols.add(FunctionName(core_words::_RECURSE, "RECURSE"));

            // Register some functions:
            sSymbols.add(Symbol(","));
            sSymbols.add(FunctionName(core_words::ABS));
            sSymbols.add(FunctionName(core_words::MAX));
            sSymbols.add(FunctionName(core_words::MIN));
        }


        // implementation of unary `-`:
        static StackEffect parseUnaryMinus(Parser &parser) {
            if (auto arg = parser.tokens().peek(); arg.type == Token::Number) {
                parser.tokens().consumePeeked();
                parser.compileLiteral(-arg.numberValue);
                return "-- #"_sfx;
            } else {
                parser.compileCall(core_words::ZERO);
                auto effect = parser.nextExpression(50_pri);
                if (effect.inputCount() != 0 || effect.outputCount() != 1)
                    parser.fail("Invalid operand for prefix `-`");
                parser.compileCall(core_words::MINUS);
                return core_words::ZERO.stackEffect() | effect | core_words::MINUS.stackEffect();
            }
        }

    private:
        static inline std::once_flag sInitOnce;
        static inline SymbolTable sSymbols;
    };

}
