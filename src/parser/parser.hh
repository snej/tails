//
// parser.hh
//
// 
//

#pragma once
#include "symbol.hh"
#include "effect_stack.hh"
#include "tokenizer.hh"
#include "value.hh"

namespace tails {
    class Compiler;


    /// A top-down operator-precedence (Pratt) parser.
    /// Most specific behavior is defined by the Symbols in its table.
    class Parser {
    public:
        explicit Parser(SymbolTable const& symbols)
        :_symbols(&symbols)
        ,_tokens(_symbols)
        { }

        void setStackEffect(StackEffect const&);

        /// Parses the source code.
        /// \note Only call this once. Use a new Parser instance every time.
        CompiledWord parse(std::string const& sourceCode);

        // ---- Methods below this point are to be called by Symbol parse methods

        /// Main parse function. Parses and compiles an expression until it reaches an operator
        /// with lower priority than `minPriority`. Returns the expression's StackEffect.
        StackEffect nextExpression(priority_t minPriority);

        /// Consumes the next token if its literal value matches; else fails.
        void requireToken(std::string_view);

        /// Consumes the next token and returns true if its literal value matches; else returns false.
        bool ifToken(std::string_view);

        /// Compiles a call to a Word.
        void compileCall(Word const&);

        /// Compiles an instruction to push the given value onto the stack.
        StackEffect compileLiteral(Value);

        /// Compiles an instruction to push an param or local value onto the stack.
        StackEffect compileGetArg(TypeSet type, int stackPos);

        /// Compiles an instruction to set a param/local's value from a value popped from the stack.
        StackEffect compileSetArg(TypeSet type, int stackPos);

        void compileROTn(int n);

        /// Throws a `compile_error`.
        [[noreturn]] void fail(std::string&& message);

        /// The tokenizer.
        Tokenizer& tokens()     {return _tokens;}
        /// The Compiler.
        Compiler& compiler()    {return *_compiler;}
        /// The Symbol table.
        SymbolTable& symbols()  {return _symbols;}

    protected:
        /// Called by the `parse` method. Can be overriden to do special top-level parsing.
        virtual StackEffect parseTopLevel() {
            return nextExpression(priority_t::None);
        }

        Tokenizer&& giveTokens()       {return std::move(_tokens);}
        void takeTokens(Tokenizer&& t) {_tokens = std::move(t);}

        SymbolTable                 _symbols;
        StackEffect                 _effect;
        Tokenizer                   _tokens;
        std::unique_ptr<Compiler>   _compiler;
        EffectStack                 _stack;
    };

}
