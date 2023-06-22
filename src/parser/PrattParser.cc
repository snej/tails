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
    ,_value(&word)
    { }

    Symbol::Symbol(Value val)
    :token("")
    ,_value(val)
    { }

    Symbol::Symbol(const char* paramName, FnParam param)
    :token(paramName)
    ,_value(param)
    { }

    Symbol::Symbol(string const& token)
    :token(token)
    { }

    Symbol::~Symbol() = default;

    Symbol&& Symbol::makePrefix(priority_t priority) && {
        prefixPriority = priority;
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
        _value = &infixWord;
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

        auto &word = _prefixWord ? *_prefixWord : this->word();
        assert(word.stackEffect().inputCount() == lhsEffect.outputCount());
        parser.add(word);
        return lhsEffect | word.stackEffect();
    }

    StackEffect Symbol::parseInfix(StackEffect const& lhsEffect, Parser& parser) const {
        if (_customParseInfix)
            return _customParseInfix(lhsEffect, parser);

        StackEffect rhsEffect = parser.nextExpression(leftPriority);

        StackEffect inputEffect = lhsEffect | rhsEffect;
//        cout << "Infix 1: combined " << lhsEffect << " | " << rhsEffect << " --> " << inputEffect << endl;
        auto &word = this->word();
        assert(word.stackEffect().inputCount() == inputEffect.outputCount());
        parser.add(word);
        auto result = inputEffect | word.stackEffect();
//        cout << "Infix 2: combined " << inputEffect << " | " << word.stackEffect() << " --> " << result << endl;
//        cout << "Infix effect is " << result << endl;//TEMP
        return result;
    }

    StackEffect Symbol::parsePostfix(StackEffect const& lhsEffect, Parser& parser) const {
        if (_customParsePostfix)
            return _customParsePostfix(lhsEffect, parser);

        // same as parsePrefix but without the call to nextExpression since the LHS has been parsed.
        auto &word = this->word();
        assert(word.stackEffect().inputCount() == lhsEffect.outputCount());
        parser.add(word);
        return lhsEffect | word.stackEffect();
    }



    void SymbolRegistry::add(Symbol && symbol) {
        _registry.emplace(symbol.token, move(symbol));
    }

    Symbol const* SymbolRegistry::get(string_view literal) const {
        if (auto i = _registry.find(string(literal)); i != _registry.end())
            return &i->second;
        else if (_parent)
            return _parent->get(literal);
        else
            return nullptr;
    }


    
    CompiledWord Parser::parse(string const& sourceCode, StackEffect const& effect) {
        _tokens.reset(sourceCode);
        _compiler = make_unique<Compiler>();
        _compiler->setStackEffect(effect);
        _stack = EffectStack();

        __unused auto exprEffect = nextExpression(priority_t::None); // Parse it all!
//        cout << "Final effect is " << exprEffect << endl;//TEMP
        if (!_tokens.atEnd())
            fail("Expected input to end here");
        _compiler->popParams();
        return std::move(*_compiler).finish();
    }

    StackEffect Parser::literal(Value literal) {
        _compiler->add(literal);
        _stack.add(literal);
        return StackEffect({}, {literal.type()});
    }

    void Parser::add(Word const& word) {
        _compiler->add(word);
        _stack.add(word, word.stackEffect(), _tokens.position());
    }

    StackEffect Parser::addParameter(FnParam param) {
        _compiler->addFnParam(param.stackPos, _tokens.position());
        StackEffect effect({}, {param.type});
        _stack.add(core_words::_PARAM, effect, _tokens.position());
        return effect;
    }


    StackEffect Parser::nextExpression(priority_t minPriority) {
        StackEffect lhs;
        switch (Token firstTok = _tokens.next(); firstTok.type) {
            case Token::End:
                fail("Unexpected end of input");
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
                    fail("Unknown symbol " + string(firstTok.literal));
                } else if (symbol->isLiteral()) {
                    lhs = literal(symbol->literalValue());
                } else if (symbol->isParameter()) {
                    lhs = addParameter(symbol->parameter());
                } else if (symbol->isPrefix()) {
                    lhs = symbol->parsePrefix(*this);
                } else {
                    fail(symbol->token + " cannot begin an expression");
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
                    fail("Expected an operator");
                case Token::Identifier:
                case Token::Operator: {
                    Symbol const* symbol = _registry.get(op.literal);
                    if (!symbol)
                        fail("Unknown symbol “" + string(op.literal) + "”");
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

    [[noreturn]] void Parser::fail(std::string&& message) {
        throw compile_error(std::move(message), _tokens.position());
    }


}
