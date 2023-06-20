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


    static constexpr const char* const kExpressionTypeNames[] = {
        "none", "literal", "variable", "+", "-", "*", "/", "==", ":=", "if", "block"
    };

    std::ostream& operator<<(std::ostream& out, Expression const& expr) {
        if (expr.type == Expression::Literal) {
            out << expr.value;
        } else if (expr.type == Expression::Variable) {
            out << expr.identifier;
        } else {
            out << kExpressionTypeNames[int(expr.type)];
            out << '(';
            int n = 0;
            for (auto &e : expr.params) {
                if (n++) out << ", ";
                out << e;
            }
            out << ')';
        }
        return out;
    }


    Symbol::Symbol(string literal_, Expression::Type type_)
    :type(type_)
    ,literal(move(literal_))
    { }

    Symbol::~Symbol() = default;

    Symbol&& Symbol::makePrefix(priority_t priority) && {
        prefixPriority = priority;
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


    Expression Symbol::parsePrefix(Parser& parser) const {
        if (_customParsePrefix)
            return _customParsePrefix(parser);
        Expression rhs = parser.nextExpression(prefixPriority);
        return Expression{.type = type, .params = {rhs}};
    }

    Expression Symbol::parseInfix(Expression&& lhs, Parser& parser) const {
        if (_customParseInfix)
            return _customParseInfix(move(lhs), parser);
        Expression rhs = parser.nextExpression(leftPriority);
        return Expression{.type = type, .params = {lhs, rhs}};
    }

    Expression Symbol::parsePostfix(Expression&& lhs, Parser& parser) const {
        if (_customParsePostfix)
            return _customParsePostfix(move(lhs), parser);
        return Expression{.type = type, .params = {lhs}};
    }



    void SymbolRegistry::add(Symbol && symbol) {
        _registry.emplace(symbol.literal, move(symbol));
    }

    Symbol const* SymbolRegistry::get(string_view literal) const {
        if (auto i = _registry.find(string(literal)); i != _registry.end())
            return &i->second;
        else
            return nullptr;
    }


    
    Expression Parser::parse(string const& sourceCode) {
        _tokens.reset(sourceCode);
        auto expr = nextExpression(priority_t::None);
        if (!_tokens.atEnd())
            throw compile_error("Expected input to end here", _tokens.position());
        return expr;
    }

    Expression Parser::nextExpression(priority_t minPriority) {
        Expression lhs;
        switch (Token firstTok = _tokens.next(); firstTok.type) {
            case Token::End:
                throw compile_error("Unexpected end of input", _tokens.position());
            case Token::Number:
                lhs = Expression{.type = Expression::Literal, .value = firstTok.numberValue};
                break;
            case Token::String:
                lhs = Expression{.type = Expression::Literal, .value = firstTok.stringValue.c_str()};
                break;
            case Token::Identifier:
            case Token::Operator: {
                Symbol const* symbol = _registry.get(firstTok.literal);
                if (!symbol) {
                    throw compile_error("Unknown symbol " + string(firstTok.literal),
                                        _tokens.position());
                } else if (symbol->type == Expression::Literal) {
                    lhs = Expression{
                        .type = Expression::Literal,
                        .identifier = string(firstTok.literal)};
                } else if (symbol->type == Expression::Variable) {
                    lhs = Expression{
                        .type = Expression::Variable,
                        .identifier = string(symbol->literal)};
                } else if (symbol->isPrefix()) {
                    lhs = symbol->parsePrefix(*this);
                } else {
                    throw compile_error(symbol->literal + " is not a prefix operator",
                                        _tokens.position());
                }
            }
        }

        while (true) {
            switch (Token const& op = _tokens.peek(); op.type) {
                case Token::End:
                    return lhs;
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
                            return lhs;
                        _tokens.consumePeeked();
                        lhs = symbol->parsePostfix(move(lhs), *this);
                    } else if (symbol->isInfix()) {
                        if (symbol->leftPriority < minPriority)
                            return lhs;
                        _tokens.consumePeeked();
                        lhs = symbol->parseInfix(move(lhs), *this);
                    } else {
                        return lhs;
                    }
                }
            }
        }
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
