//
// parser.hh
//
// 
//

#pragma once
#include "stack_effect.hh"
#include "effect_stack.hh"
#include "tokenizer.hh"
#include "value.hh"
#include <functional>
#include <unordered_map>
#include <variant>

namespace tails {
    class Compiler;
    class Parser;

    enum class priority_t : int { None = INT_MIN };
    static inline priority_t operator""_pri (unsigned long long v) {return priority_t(int(v));}

    // A symbol in the grammar: defines how to parse it.
    class Symbol {
    public:
        explicit Symbol(Word const&);
        explicit Symbol(Value);
        explicit Symbol(std::string const& token);
        explicit Symbol(const char* token)              :Symbol(std::string(token)) { }
        virtual ~Symbol();

        std::string token;

        bool isLiteral() const      {return std::holds_alternative<Value>(_value);}
        Value literalValue() const  {return std::get<Value>(_value);}

        bool isWord() const         {return std::holds_alternative<Word const*>(_value);}
        Word const& word() const    {return *std::get<Word const*>(_value);}

        using ParsePrefixFn  = std::function<StackEffect(Parser&)>;
        using ParseInfixFn   = std::function<StackEffect(StackEffect const&, Parser&)>;
        using ParsePostfixFn = ParseInfixFn;

        Symbol&& makePrefix(priority_t) &&;
        Symbol&& makePrefix(priority_t, Word const&) &&;
        Symbol&& makePrefix(priority_t, ParsePrefixFn) &&;

        Symbol&& makeInfix(priority_t left, priority_t right) &&;
        Symbol&& makeInfix(priority_t left, priority_t right, Word const&) &&;
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

        virtual StackEffect parsePrefix(Parser&) const;
        virtual StackEffect parseInfix(StackEffect const&, Parser&) const;
        virtual StackEffect parsePostfix(StackEffect const&, Parser&) const;

    private:
        std::variant<nullptr_t,Word const*,Value> _value;
        Word const*     _prefixWord = nullptr;          // in case Word is different when prefix
        ParsePrefixFn   _customParsePrefix;
        ParseInfixFn    _customParseInfix;
        ParsePostfixFn  _customParsePostfix;
    };


    class FnParam : public Symbol {
    public:
        explicit FnParam(const std::string& paramName, TypeSet type, int stackPos);
        StackEffect parsePrefix(Parser&) const override;
    private:
        TypeSet _type;
        int _stackPos;  // offset from top of stack at fn entry: 0 = last arg, -1 = prev arg,...
    };


    class SymbolTable {
    public:
        explicit SymbolTable(SymbolTable const* parent =nullptr) :_parent(parent) { }

        void addPtr(std::unique_ptr<Symbol>);

        template <class SYM>
        void add(SYM&& symbol) {
            addPtr(std::make_unique<SYM>(std::move(symbol)));
        }

        void reset()                            {_registry.clear();}

        Symbol const* get(std::string_view) const;
        bool has(std::string_view name) const    {return get(name) != nullptr;}
        bool itselfHas(std::string_view) const;

    private:
        SymbolTable const* _parent;
        std::unordered_map<std::string_view, unique_ptr<Symbol>> _registry;
    };


    class Parser {
    public:
        explicit Parser(SymbolTable const& symbols)
        :_symbols(&symbols)
        ,_tokens(_symbols)
        { }

        void addParam(FnParam p)    {_symbols.add(std::move(p));}
        void setStackEffect(StackEffect const& e)   {_effect = e;}

        /// Parses the source code.
        /// \note Only call this once. Use a new Parser instance every time.
        CompiledWord parse(std::string const& sourceCode);

        StackEffect nextExpression(priority_t minPriority);

        Tokenizer& tokens()     {return _tokens;}
        Compiler& compiler()    {return *_compiler;}
        SymbolTable& symbols() {return _symbols;}

        // Consumes the next token if its literal value matches; else fails.
        void requireToken(std::string_view);

        // Consumes the next token and returns true if its literal value matches; else returns false.
        bool ifToken(std::string_view);

        void add(Word const&);
        StackEffect addGetArg(TypeSet type, int stackPos);
        StackEffect addSetArg(TypeSet type, int stackPos);

        [[noreturn]] void fail(std::string&& message);

    protected:
        virtual StackEffect parseTopLevel() {
            return nextExpression(priority_t::None);
        }

        StackEffect literal(Value);
        
        SymbolTable _symbols;
        StackEffect _effect;
        Tokenizer _tokens;
        std::unique_ptr<Compiler> _compiler;
        EffectStack _stack;
    };

}
