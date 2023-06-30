//
// symbol.hh
//
// 
//

#pragma once
#include "stack_effect.hh"
#include "value.hh"
#include <functional>
#include <unordered_map>
#include <variant>

namespace tails {
    class Compiler;
    class Parser;

    /// Binding priority of an operator in a Pratt parser.
    enum class priority_t : int { None = INT_MIN };
    static inline priority_t operator""_pri (unsigned long long v) {return priority_t(int(v));}


    /// A grammar symbol representing an identifier or operator: defines how to parse it.
    class Symbol {
    public:
        explicit Symbol(Word const& word);
        explicit Symbol(Word const&, std::string token);
        explicit Symbol(Value);
        explicit Symbol(std::string token);
        explicit Symbol(const char* token)              :Symbol(std::string(token)) { }
        virtual ~Symbol();

        /// This Symbol's textual representation.
        std::string token;

        /// True if the Symbol represents a literal Value.
        bool isLiteral() const      {return std::holds_alternative<Value>(_value);}
        Value literalValue() const  {return std::get<Value>(_value);}

        /// True if the Symbol is the name of a Tails Word.
        bool isWord() const         {return std::holds_alternative<Word const*>(_value);}
        Word const& word() const    {return *std::get<Word const*>(_value);}

        using ParsePrefixFn  = std::function<StackEffect(Parser&)>;
        using ParseInfixFn   = std::function<StackEffect(StackEffect const&, Parser&)>;
        using ParsePostfixFn = ParseInfixFn;

        /// Makes the Symbol a prefix operator in the grammar.
        Symbol&& makePrefix(priority_t) &&;
        Symbol&& makePrefix(priority_t, Word const&) &&;
        Symbol&& makePrefix(priority_t, ParsePrefixFn) &&;

        /// Makes the Symbol an infix operator in the grammar.
        Symbol&& makeInfix(priority_t left, priority_t right) &&;
        Symbol&& makeInfix(priority_t left, priority_t right, Word const&) &&;
        Symbol&& makeInfix(priority_t left, priority_t right, ParseInfixFn) &&;

        /// Makes the Symbol a postfix operator in the grammar.
        Symbol&& makePostfix(priority_t) &&;
        Symbol&& makePostfix(priority_t, ParsePostfixFn) &&;

        /// True if the Symbol can be parsed in prefix position.
        bool isPrefix() const       {return prefixPriority != priority_t::None;}
        /// True if the Symbol can be parsed in infix position.
        bool isInfix() const        {return leftPriority != priority_t::None;}
        /// True if the Symbol can be parsed in postfix position.
        bool isPostfix() const      {return postfixPriority != priority_t::None;}

        priority_t prefixPriority  = priority_t::None;  ///< Priority as prefix operator
        priority_t leftPriority    = priority_t::None;  ///< Left-binding priority as infix operator
        priority_t rightPriority   = priority_t::None;  ///< Right-binding priority as infix operator
        priority_t postfixPriority = priority_t::None;  ///< Priority as postfix operator

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



    /// A dictionary of Symbols.
    class SymbolTable {
    public:
        /// Constructs a SymbolTable. It is empty but inherits entries from its parent.
        explicit SymbolTable(SymbolTable const* parent =nullptr) :_parent(parent) { }

        /// Adds a symbol. The Symbol object is copied.
        template <class SYM>
        void add(SYM&& symbol) {
            addPtr(std::make_unique<SYM>(std::move(symbol)));
        }

        /// Removes all symbols defined in this table (but not its parent.)
        void reset()                            {_registry.clear();}

        /// Looks up a symbol in this table and its ancestors. Returns nullptr if not found.
        Symbol const* get(std::string_view) const;

        /// Returns true if the symbol exists in this table or its ancestors.
        bool has(std::string_view name) const    {return get(name) != nullptr;}

        /// Returns true if this symbol exists in this table; does not consult parent.
        bool itselfHas(std::string_view) const;

    private:
        void addPtr(std::unique_ptr<Symbol>);
        SymbolTable const* _parent;
        std::unordered_map<std::string, std::unique_ptr<Symbol>> _registry;
    };

}
