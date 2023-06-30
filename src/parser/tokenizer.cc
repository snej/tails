//
// tokenizer.cc
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

#include "tokenizer.hh"
#include "parser.hh"
#include "compiler.hh"

namespace tails {
    using namespace std;


    /// Returns a pointer to the next UTF-8 character after 'pos'.
    static inline const char* nextChar(const char* pos) {
        if (*pos++ & 0x80) {
            while (*pos & 0x80)
                ++pos;
        }
        return pos;
    }


    void Tokenizer::reset(std::string const& sourceCode) {
        _hasToken = false;
        _curPos = _next = &sourceCode[0];
    }


    void Tokenizer::skipWhitespace() {
        while (*_next != 0 && isspace(*_next))
            ++_next;
    }

    
    char Tokenizer::peekChar() {
        skipWhitespace();
        return *_next;
    }


    const char* Tokenizer::skipThrough(char c) {
        const char* end = strchr(_next, c);
        if (!end)
            return nullptr;
        _next = ++end;
        _hasToken = false;
        return end;
    }


    Token Tokenizer::next() {
        if (!_hasToken)
            readToken();
        _hasToken = false;
        return std::move(_cur);
    }


    void Tokenizer::readToken() {
        skipWhitespace();
        auto start = _next;
        _curPos = start;
        if (char c = *start; c == 0) {
            // EOF:
            _cur = Token{Token::End};
        } else if (c == '"') {
            // String literal:
            _cur = Token{.type = Token::String};
            auto pieceStart = _next + 1;
            do {
                c = *++_next;
                if (c == 0)
                    throw compile_error("Unclosed string literal", _next);
                else if (c == '\\') {
                    _cur.stringValue += string_view(pieceStart,_next);
                    char escaped = *++_next;
                    if (escaped == 0)
                        throw compile_error("Unclosed string literal", _next);
                    _cur.stringValue += escaped;    //TODO: Handle \n, etc.
                    pieceStart = ++_next;
                }
            } while (c != '"');
            _cur.stringValue += string_view(pieceStart,_next);
            ++_next; // include the trailing quote
        } else if (isdigit(c) || (c == '-' && isdigit(*_next))) {
            // Numeric literal:
            char* end;
            double n = ::strtod(start, &end);
            if (isnan(n) || isinf(n))
                throw compile_error("Invalid number", start);
            _next = end;
            _cur = Token{
                .type = Token::Number,
                .numberValue = n,
            };
        } else if (isalpha(c) || c == '_') {
            // Identifier:
            do {
                c = *++_next;
            } while (isalpha(c) || isdigit(c) || c == '_');
            if (c == ':')
                ++_next;        // Identifier may end in ':'
            _cur = Token{
                .type = Token::Identifier,
            };
        } else {
            // Other symbol -- look for one that's registered:
            _next = readSymbolAt(_next);
            if (!_next)
                throw compile_error("Unknown token “" + string(start, _next) + "”", start);
            _cur = Token{
                .type = Token::Operator,
            };
        }
        _cur.literal = string_view(start, _next);
        _hasToken = true;
    }


    const char* Tokenizer::readSymbolAt(const char *start) {
        if (_symbols) {
            // Look for the longest matching registered symbol, up to 3 UTF-8 code points long:
            const char* pos = start;
            const char* end = nullptr;
            for (int len = 1; len <= 3 && *pos; ++len) {
                pos = nextChar(pos);
                if (_symbols->get(string_view(start, pos)))
                    end = pos;
            }
            return end;
        } else {
            // Without a symbol table, just return a single UTF-8 character:
            return nextChar(start);
        }
    }

}
