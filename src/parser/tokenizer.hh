//
// tokenizer.hh
//
// 
//

#pragma once
#include <string>
#include <string_view>

namespace tails {
    class SymbolRegistry;

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
        Tokenizer(SymbolRegistry const& reg)    :_symbols(&reg) { }
        
        void reset(std::string const& sourceCode);

        Token const& peek() const       {return _cur;}
        void consumePeeked()            {readToken();}

        Token next()                    {Token cur = std::move(_cur); readToken(); return cur;}

        bool atEnd() const              {return _cur.type == Token::End;}

        const char* position() const    {return _curPos;}

    private:
        void readToken();
        void skipWhitespace();
        char peekChar();

        SymbolRegistry const* _symbols = nullptr;
        const char* _next;
        Token       _cur;
        const char* _curPos;
    };


}
