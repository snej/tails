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

#include "parser.hh"
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
        parser.compileCall(word);
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
        parser.compileCall(word);
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
        parser.compileCall(word);
        return lhsEffect | word.stackEffect();
    }


#pragma mark - SYMBOL REGISTRY:


    void SymbolTable::addPtr(unique_ptr<Symbol> symbol) {
        _registry.emplace(symbol->token, move(symbol));
    }

    Symbol const* SymbolTable::get(string_view literal) const {
        if (auto i = _registry.find(literal); i != _registry.end())
            return i->second.get();
        else if (_parent)
            return _parent->get(literal);
        else
            return nullptr;
    }

    bool SymbolTable::itselfHas(std::string_view name) const {
        return _registry.find(name) != _registry.end();
    }



#pragma mark - PARSER:


    CompiledWord Parser::parse(string const& sourceCode) {
        _tokens.reset(sourceCode);
        assert(!_compiler);
        _compiler = make_unique<Compiler>();
        _compiler->setStackEffect(_effect);
        _compiler->preservesArgs();

        __unused auto exprEffect = parseTopLevel(); // Parse it all!
        if (!_tokens.atEnd())
            fail("Expected input to end here");

        return std::move(*_compiler).finish();
    }


    void Parser::setStackEffect(StackEffect const& e) {
        _effect = e;
        if (_compiler)
            _compiler->setStackEffect(e);
    }


    // This is the core Pratt parser algorithm.
    StackEffect Parser::nextExpression(priority_t minPriority) {
        StackEffect effect;
        switch (Token firstTok = _tokens.next(); firstTok.type) {
            case Token::End:
                fail("Unexpected end of input");
            case Token::Number:
                effect = compileLiteral(Value(firstTok.numberValue));
                break;
            case Token::String:
                effect = compileLiteral(Value(firstTok.stringValue.c_str()));
                break;
            case Token::Identifier:
            case Token::Operator: {
                Symbol const* symbol = _symbols.get(firstTok.literal);
                if (!symbol) {
                    fail("Unknown symbol “" + string(firstTok.literal) + "”");
                } else if (symbol->isLiteral()) {
                    effect = compileLiteral(symbol->literalValue());
                } else if (symbol->isPrefix()) {
                    effect = symbol->parsePrefix(*this);
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
                    Symbol const* symbol = _symbols.get(op.literal);
                    if (!symbol)
                        fail("Unknown symbol “" + string(op.literal) + "”");
                    else if (symbol->isPostfix()) {
                        if (symbol->postfixPriority < minPriority)
                            goto exit;
                        _tokens.consumePeeked();
                        effect = symbol->parsePostfix(effect, *this);
                    } else if (symbol->isInfix()) {
                        if (symbol->leftPriority < minPriority)
                            goto exit;
                        _tokens.consumePeeked();
                        effect = symbol->parseInfix(effect, *this);
                    } else {
                        goto exit;
                    }
                }
            }
        }
    exit:
        return effect;
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


    StackEffect Parser::compileLiteral(Value literal) {
        _compiler->add(literal);
        _stack.add(literal);
        return StackEffect({}, {literal.type()});
    }

    void Parser::compileCall(Word const& word) {
        _compiler->add(word);
        _stack.add(word, word.stackEffect(), _tokens.position());
    }

    StackEffect Parser::compileGetArg(TypeSet type, int stackPos) {
        _compiler->addGetArg(stackPos, _tokens.position());
        StackEffect effect({}, {type});
        _stack.add(core_words::_GETARG, effect, _tokens.position());
        return effect;
    }

    StackEffect Parser::compileSetArg(TypeSet type, int stackPos) {
        _compiler->addSetArg(stackPos, _tokens.position());
        StackEffect effect({type}, {});
        _stack.add(core_words::_SETARG, effect, _tokens.position());
        return effect;
    }

}
