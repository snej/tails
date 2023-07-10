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
        _stack.push(literal);
        return StackEffect({}, {literal.type()});
    }

    void Parser::compileCall(Word const& word) {
        if (word == core_words::_RECURSE) {
            _compiler->addRecurse();
            _stack.add(word, _effect, _tokens.position());
        } else {
            _compiler->add(word);
            _stack.add(word, word.stackEffect(), _tokens.position());
        }
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
