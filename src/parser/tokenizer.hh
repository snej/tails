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
            Number,
            String,
            Identifier,
            Operator,
            End,
        };

        Type                type;
        std::string_view    literal;
        std::string         stringValue;    // only for type String
        double              numberValue;    // only for type Number

        explicit operator bool() const {return type != End;}
    };


    class Tokenizer {
    public:
        Tokenizer() = default;
        Tokenizer(SymbolTable const& reg)    :_symbols(&reg) { }
        
        void reset(std::string const& sourceCode);

        Token const& peek()             {if (!_hasToken) readToken(); return _cur;}
        void consumePeeked()            {_hasToken = false; _curPos = _next;}

        Token next() {
            if (!_hasToken)
                readToken();
            _hasToken = false;
            return std::move(_cur);
        }

        bool atEnd()                    {return peek().type == Token::End;}

        const char* position() const    {return _curPos;}

        /// Skips ahead through the next occurrence of `c`, and returns a pointer to the next
        /// character after it. If `c` is not found, returns nullptr.
        const char* skipThrough(char c);

    private:
        void readToken();
        void skipWhitespace();
        char peekChar();

        SymbolTable const* _symbols = nullptr;
        Token       _cur;       // Current token
        const char* _curPos;    // Start of current token, if parsed
        const char* _next;      // Start of next token, not yet parsed
        bool        _hasToken;
    };


}
