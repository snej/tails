//
// PrattParser.cc
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
#include "value.hh"
#include "io.hh"
#include <iostream>

namespace tails {
    using namespace std;


    Symbol::Symbol(Word const& word)
    :token(word.name())
    ,_word(&word)
    { }

    Symbol::Symbol(Value val)
    :token("")
    ,_literal(val)
    { }

    Symbol::Symbol(string const& token)
    :token(token)
    { }

    Symbol::~Symbol() = default;

    Symbol&& Symbol::makePrefix(priority_t priority) && {
        prefixPriority = priority;
        _prefixWord = _word;
        return move(*this);
    }

    Symbol&& Symbol::makePrefix(priority_t priority, Word const& prefixWord) && {
        prefixPriority = priority;
        _prefixWord = &prefixWord;
        return move(*this);
    }

    Symbol&& Symbol::makePrefix(priority_t priority, ParsePrefixFn parse) && {
        prefixPriority = priority;
        _customParsePrefix = parse;
        return move(*this);
    }

    Symbol&& Symbol::makeInfix(priority_t left, priority_t right) && {
        leftPriority = left;
        rightPriority = right;
        return move(*this);
    }

    Symbol&& Symbol::makeInfix(priority_t left, priority_t right, Word const& infixWord) && {
        leftPriority = left;
        rightPriority = right;
        _word = &infixWord;
        return move(*this);
    }

    Symbol&& Symbol::makeInfix(priority_t left, priority_t right, ParseInfixFn parse) && {
        leftPriority = left;
        rightPriority = right;
        _customParseInfix = parse;
        return move(*this);
    }

    Symbol&& Symbol::makePostfix(priority_t priority) && {
        postfixPriority = priority;
        return move(*this);
    }

    Symbol&& Symbol::makePostfix(priority_t priority, ParsePostfixFn parse) && {
        postfixPriority = priority;
        _customParsePostfix = parse;
        return move(*this);
    }


    StackEffect Symbol::parsePrefix(Parser& parser) const {
        if (_customParsePrefix)
            return _customParsePrefix(parser);

        StackEffect lhsEffect = parser.nextExpression(prefixPriority);

        auto word = _prefixWord ? _prefixWord : _word;
        assert(word);
        assert(word->stackEffect().inputCount() == lhsEffect.outputCount());
        parser.compiler().add(word);
        return lhsEffect | word->stackEffect();
    }

    StackEffect Symbol::parseInfix(StackEffect const& lhsEffect, Parser& parser) const {
        if (_customParseInfix)
            return _customParseInfix(lhsEffect, parser);

        StackEffect rhsEffect = parser.nextExpression(leftPriority);

        StackEffect inputEffect = lhsEffect | rhsEffect;
//        cout << "Infix 1: combined " << lhsEffect << " | " << rhsEffect << " --> " << inputEffect << endl;
        assert(_word);
        assert(_word->stackEffect().inputCount() == inputEffect.outputCount());
        parser.compiler().add(_word);
        auto result = inputEffect | _word->stackEffect();
//        cout << "Infix 2: combined " << inputEffect << " | " << _word->stackEffect() << " --> " << result << endl;
//        cout << "Infix effect is " << result << endl;//TEMP
        return result;
    }

    StackEffect Symbol::parsePostfix(StackEffect const& lhsEffect, Parser& parser) const {
        if (_customParsePostfix)
            return _customParsePostfix(lhsEffect, parser);

        // same as parsePrefix but without the call to nextExpression since the LHS has been parsed.
        assert(_word);
        assert(_word->stackEffect().inputCount() == lhsEffect.outputCount());
        parser.compiler().add(_word);
        return lhsEffect | _word->stackEffect();
    }



    void SymbolRegistry::add(Symbol && symbol) {
        _registry.emplace(symbol.token, move(symbol));
    }

    Symbol const* SymbolRegistry::get(string_view literal) const {
        if (auto i = _registry.find(string(literal)); i != _registry.end())
            return &i->second;
        else
            return nullptr;
    }


    
    CompiledWord Parser::parse(string const& sourceCode) {
        _tokens.reset(sourceCode);
        _compiler = make_unique<Compiler>();
        auto effect = nextExpression(priority_t::None);
//        cout << "Final effect is " << effect << endl;//TEMP
        _compiler->setStackEffect(effect);
        if (!_tokens.atEnd())
            throw compile_error("Expected input to end here", _tokens.position());
        return std::move(*_compiler).finish();
    }

    StackEffect Parser::literal(Value literal) {
        _compiler->add(literal);
        return StackEffect({}, {literal.type()});
    }

    StackEffect Parser::nextExpression(priority_t minPriority) {
        StackEffect lhs;
        switch (Token firstTok = _tokens.next(); firstTok.type) {
            case Token::End:
                throw compile_error("Unexpected end of input", _tokens.position());
            case Token::Number:
                lhs = literal(Value(firstTok.numberValue));
                break;
            case Token::String:
                lhs = literal(Value(firstTok.stringValue.c_str()));
                break;
            case Token::Identifier:
            case Token::Operator: {
                Symbol const* symbol = _registry.get(firstTok.literal);
                if (!symbol) {
                    throw compile_error("Unknown symbol " + string(firstTok.literal),
                                        _tokens.position());
                } else if (symbol->isLiteral()) {
                    lhs = literal(symbol->literalValue());
                } else if (symbol->isPrefix()) {
                    lhs = symbol->parsePrefix(*this);
                } else {
                    throw compile_error(symbol->token + " cannot begin an expression",
                                        _tokens.position());
                }
                break;
            }
        }

        while (true) {
            switch (Token const& op = _tokens.peek(); op.type) {
                case Token::End:
                    goto exit;
                case Token::Number:
                case Token::String:
                    throw compile_error("Expected an operator", _tokens.position());
                case Token::Identifier:
                case Token::Operator: {
                    Symbol const* symbol = _registry.get(op.literal);
                    if (!symbol)
                        throw compile_error("Unknown symbol “" + string(op.literal) + "”",
                                            _tokens.position());
                    else if (symbol->isPostfix()) {
                        if (symbol->postfixPriority < minPriority)
                            goto exit;
                        _tokens.consumePeeked();
                        lhs = symbol->parsePostfix(lhs, *this);
                    } else if (symbol->isInfix()) {
                        if (symbol->leftPriority < minPriority)
                            goto exit;
                        _tokens.consumePeeked();
                        lhs = symbol->parseInfix(lhs, *this);
                    } else {
                        goto exit;
                    }
                }
            }
        }
        exit:
//        cout << "nextExpression: effect is " << lhs << endl;//TEMP
        return lhs;
    }

    bool Parser::ifToken(std::string_view literal) {
        if (_tokens.peek().literal != literal)
            return false;
        _tokens.consumePeeked();
        return true;
    }

    void Parser::requireToken(std::string_view literal) {
        if (_tokens.peek().literal == literal)
            _tokens.consumePeeked();
        else
            throw compile_error("expected “" + string(literal) + "”", _tokens.position());
    }



}
