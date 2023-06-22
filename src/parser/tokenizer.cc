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
#include "PrattParser.hh"
#include "compiler.hh"

namespace tails {
    using namespace std;


    void Tokenizer::reset(std::string const& sourceCode) {
        _next = &sourceCode[0];
        readToken(); // populates _cur
    }


    void Tokenizer::skipWhitespace() {
        while (*_next != 0 && isspace(*_next))
            ++_next;
    }

    
    char Tokenizer::peekChar() {
        skipWhitespace();
        return *_next;
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
            ++_next;
            if (_symbols) {
                const char* end = nullptr;
                for (int len = 1; len <= 3; ++len) {
                    if (_symbols->get(string_view(start, len)))
                        end = start + len;
                }
                if (!end)
                    throw compile_error("Unknown token “" + string(start, 1) + "”", start);
                _next = end;
            }
            _cur = Token{
                .type = Token::Operator,
            };
        }
        _cur.literal = string_view(start, _next-start);
    }


}