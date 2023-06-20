//
// PrattParser.hh
//
// 
//

#pragma once
#include "tokenizer.hh"
#include "value.hh"
#include <functional>
#include <unordered_map>
#include <vector>

namespace tails {
    class Parser;

    struct Expression {
        enum Type {
            None,
            Literal,
            Variable,
            Add, Subtract, Multiply, Divide,
            Equals,
            Assign,
            If,
            Block,
        };

        Type type;
        Value value;
        std::string identifier;
        std::vector<Expression> params;

        friend std::ostream& operator<<(std::ostream&, Expression const&);
    };

    enum class priority_t : int { None = INT_MIN };

    static inline priority_t operator""_pri (unsigned long long v) {return priority_t(int(v));}

    // A symbol in the grammar.
    class Symbol {
    public:
        Symbol(std::string literal_, Expression::Type type);
        virtual ~Symbol();

        Expression::Type type;
        std::string literal;

        using ParsePrefixFn = std::function<Expression(Parser&)>;
        using ParseInfixFn = std::function<Expression(Expression&&,Parser&)>;
        using ParsePostfixFn = ParseInfixFn;

        Symbol&& makePrefix(priority_t) &&;
        Symbol&& makePrefix(priority_t, ParsePrefixFn) &&;

        Symbol&& makeInfix(priority_t left, priority_t right) &&;
        Symbol&& makeInfix(priority_t left, priority_t right, ParseInfixFn) &&;

        Symbol&& makePostfix(priority_t) &&;
        Symbol&& makePostfix(priority_t, ParsePostfixFn) &&;

        bool isPrefix() const       {return prefixPriority != priority_t::None;}
        bool isInfix() const        {return leftPriority != priority_t::None;}
        bool isPostfix() const      {return postfixPriority != priority_t::None;}

        priority_t prefixPriority  = priority_t::None;
        priority_t leftPriority    = priority_t::None;
        priority_t rightPriority   = priority_t::None;
        priority_t postfixPriority = priority_t::None;

        virtual Expression parsePrefix(Parser&) const;
        virtual Expression parseInfix(Expression&&,Parser&) const;
        virtual Expression parsePostfix(Expression&&,Parser&) const;

    private:
        ParsePrefixFn  _customParsePrefix;
        ParseInfixFn   _customParseInfix;
        ParsePostfixFn _customParsePostfix;
    };


    class SymbolRegistry {
    public:
        SymbolRegistry() = default;

        void add(Symbol&&);

        Symbol const* get(std::string_view) const;

    private:
        std::unordered_map<std::string, Symbol> _registry;
    };


    class Parser {
    public:
        explicit Parser(SymbolRegistry const& reg)
        :_registry(reg)
        ,_tokens(_registry)
        { }

        Expression parse(std::string const& sourceCode);

        Expression nextExpression(priority_t minPriority);

        Tokenizer& tokens()     {return _tokens;}

        // Consumes the next token if its literal value matches; else fails.
        void requireToken(std::string_view);

        // Consumes the next token and returns true if its literal value matches; else returns false.
        bool ifToken(std::string_view);

    private:
        SymbolRegistry const& _registry;
        Tokenizer _tokens;
    };

}
