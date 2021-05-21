//
// parser.cc
//
// Copyright (C) 2021 Jens Alfke. All Rights Reserved.
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

#include "compiler.hh"
#include "core_words.hh"
#include "vocabulary.hh"
#include <optional>
#include <sstream>
#include <string>
#include <string_view>


namespace tails {
    using namespace std;
    using namespace tails::core_words;


    /// Skips whitespace, then reads & returns the next token:
    /// * an empty string at EOF;
    /// * a string literal, starting and ending with double-quotes;
    /// * a "{" or "}";
    /// * a "[" or "]";
    /// * else the largest number of consecutive non-whitespace non-closing-brace characters.
    static string_view readToken(const char* &input) {
        // Skip whitespace
        while (*input != 0 && isspace(*input))
            ++input;

        // Read token
        auto start = input;
        switch (*input) {
            case 0:
                // EOF:
                break;
            case '"':
                // String literal:
                do {
                    ++input;
                    //TODO: Handle escape sequences
                } while (*input != 0 && *input != '"');
                if (*input)
                    ++input; // include the trailing quote
                break;
            case '{':
            case '[':
                // Open array or quotation -- just return the single delimiter character
                ++input;
                break;
            default:
                // General token: read until next whitespace or closing brace/bracket:
                do {
                    ++input;
                } while (*input != 0 && !isspace(*input) && *input != '}' && *input != ']');
                break;
        }
        return {start, size_t(input - start)};
    }


    /// Tries to parse `token` as an integer (decimal or hex) or floating-point number.
    /// Returns `nullopt` if it's not. Throws `compile_error` if it's an out-of-range number.
    static optional<double> asNumber(string_view token) {
        try {
            size_t pos;
            double d = stod(string(token), &pos);
            if (pos == token.size() && !isnan(d) && !isinf(d))
                return d;
        } catch (const std::out_of_range&) {
            throw compile_error("Number out of range", token.data());
        } catch (const std::invalid_argument&) {
            // if invalid number, just return nullopt
        }
        return nullopt;
    }


    void Compiler::parse(const string &input, bool allowMagic) {
        const char *remainder = parse(input.c_str(), allowMagic);
        if (*remainder != 0)
            throw compile_error("Unexpected delimiter; expected end of input", remainder);
    }


    const char* Compiler::parse(const char *input, bool allowMagic) {
        while (true) {
            string_view token = _curToken = readToken(input);
            const char *sourcePos = token.data();
            if (token.empty()) {
                // End of input
                break;

            } else if (token == "]") {
                // end of a nested word (quotation). Exit, but don't consume the ']'.
                --input;
                break;

            } else if (token[0] == '"') {
                // String literal:
                add({_LITERAL, parseString(token)}, sourcePos);

            } else if (token == "{") {
                add({_LITERAL, parseArray(input)}, token.data());

            } else if (token == "[") {
                add({_LITERAL, parseQuote(input)}, token.data());

            } else if (token == "IF") {
                // IF compiles into 0BRANCH, with offset TBD:
                pushBranch('i', &_ZBRANCH);

            } else if (token == "ELSE") {
                // ELSE compiles into BRANCH, with offset TBD, and resolves the IF's branch:
                auto ifPos = popBranch("i");
                pushBranch('e', &_BRANCH);
                fixBranch(ifPos);

            } else if (token == "THEN") {
                // THEN generates no code but completes the remaining branch from IF or ELSE:
                auto ifPos = popBranch("ie");
                fixBranch(ifPos);

            } else if (token == "BEGIN") {
                // BEGIN generates no code but remembers the current address:
                pushBranch('b');

            } else if (token == "WHILE") {
                // IF compiles into 0BRANCH, with offset TBD:
                if (_controlStack.empty() || _controlStack.back().first != 'b')
                    throw compile_error("no matching BEGIN for this WHILE", sourcePos);
                pushBranch('w', &_ZBRANCH);

            } else if (token == "REPEAT") {
                // REPEAT generates a BRANCH back to the BEGIN's position,
                // and fixes up the WHILE to point to the next instruction:
                auto whilePos = popBranch("w");
                auto beginPos = popBranch("b");
                addBranchBackTo(beginPos);
                fixBranch(whilePos);

            } else if (const Word *word = Vocabulary::global.lookup(token); word) {
                // Known word is added as an instruction:
                if (!allowMagic && word->isMagic())
                        throw compile_error("Special word " + string(token)
                                            + " cannot be added by parser", sourcePos);
                if (word->hasAnyParam()) {
                    auto numTok = readToken(input);
                    auto param = asNumber(numTok);
                    if (!param || (*param != intptr_t(*param)))
                        throw compile_error("Invalid param after " + string(token), numTok.data());
                    if (word->hasIntParam())
                        add({*word, (intptr_t)*param}, sourcePos);
                    else
                        add({*word, Value(*param)}, sourcePos);
                } else {
                    add(*word, sourcePos);
                }

            } else if (auto np = asNumber(token); np) {
                // A number is added as a LITERAL instruction:
                add({_LITERAL, Value(*np)}, sourcePos);

            } else {
                throw compile_error("Unknown word '" + string(token) + "'", sourcePos);
            }
        }
        _curToken = {};
        return input;
    }


    Value Compiler::parseString(string_view token) {
#ifdef SIMPLE_VALUE
        throw compile_error("Strings not supported", token.data());
#else
        if (token.size() == 1 || token[token.size()-1] != '"')
            throw compile_error("Unfinished string literal", token.end());
        token = token.substr(1, token.size() - 2);
        return Value(token.data(), token.size());
#endif
    }


    Value Compiler::parseArray(const char* &input) {
#ifdef SIMPLE_VALUE
        throw compile_error("Arrays not supported", input);
#else
        Value arrayVal({});
        Value::Array *array = arrayVal.asArray();
        while (true) {
            string_view token = readToken(input);
            if (token == "}")
                break;
            else if (token.empty())
                throw compile_error("Unfinished array literal", input);
            else if (token[0] == '"')
                array->push_back(parseString(token));
            else if (token == "{")
                array->push_back(parseArray(input));
            else if (auto np = asNumber(token); np)
                array->push_back(Value(*np));
            else
                throw compile_error("Invalid literal '" + string(token) + "' in array", token.data());
        }
        return arrayVal;
#endif
    }


    Value Compiler::parseQuote(const char* &input) {
#ifdef SIMPLE_VALUE
        throw compile_error("Quotes not supported", input);
#else
        // Recursively create a Compiler to parse tokens to a new Word until the ']' delimiter:
        Compiler quoteCompiler;
        input = quoteCompiler.parse(input, false);
        if (*input != ']')
            throw compile_error("Missing ']'; unfinished quotation", input);
        ++input;

        return Value(new CompiledWord(quoteCompiler));
#endif
    }

}
