//
// tokenizer.hh
//
// 
//

#pragma once
#include <string>
#include <string_view>

namespace tails {
    class SymbolTable;

    struct Token {
        enum Type {
            Number,         // Numeric literal. Typical C syntax (uses strtod)
            String,         // Double-quoted string literal
            Identifier,     // Alphanumeric identifier; `_` allowed
            Operator,       // Anything else if it's in the SymbolTable. Chooses longest match
            End,            // End of input
        };

        Type                type;           // type of token
        std::string_view    literal;        // literal value of token; points into input string
        std::string         stringValue;    // only for type String
        double              numberValue;    // only for type Number

        explicit operator bool() const {return type != End;}
    };


    /// A pretty typical C-like tokenizer. See Token::Type for details.
    /// The SymbolTable determines which punctuation characters, and even sequences thereof,
    /// are valid.
    class Tokenizer {
    public:
        Tokenizer() = default;
        Tokenizer(SymbolTable const& reg)    :_symbols(&reg) { }

        /// Starts the tokenizer, at the beginning of the string.
        /// \warning  The Tokenizer does not own the string's bytes; they must remain valid.
        void reset(std::string const& sourceCode);

        /// Returns the next token (possibly already peeked) and advances past it.
        Token next();

        /// Returns the next token but does not consume it. Idempotent.
        Token const& peek()             {if (!_hasToken) readToken(); return _cur;}

        /// Consumes the peeked token. Next call to peek or next will read a new token.
        void consumePeeked()            {_hasToken = false; _curPos = _next;}

        /// True if all the tokens have been read.
        bool atEnd()                    {return peek().type == Token::End;}

        /// Points to the start of the latest token.
        const char* position() const    {return _curPos;}

        /// Skips ahead through the next occurrence of `c`, and returns a pointer to the next
        /// character after it. If `c` is not found, returns nullptr.
        const char* skipThrough(char c);

    private:
        void readToken();
        void skipWhitespace();
        char peekChar();
        const char* readSymbolAt(const char *start);

        SymbolTable const*  _symbols = nullptr; // Defines identifiers and operators
        Token               _cur;               // Current token (if _hasToken is true)
        const char*         _curPos = nullptr;  // Start of current token
        const char*         _next = nullptr;    // Next character to be lexed
        bool                _hasToken = false;  // True if current token has been read
    };


}
