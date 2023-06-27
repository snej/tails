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
#include "stack_effect_parser.hh"
#include "vocabulary.hh"
#include <optional>
#include <sstream>
#include <string>
#include <string_view>


namespace tails {
    using namespace std;
    using namespace tails::core_words;


    static void skipWhitespace(const char* &input) {
        while (*input != 0 && isspace(*input))
            ++input;
    }

    static char peek(const char* &input) {
        skipWhitespace(input);
        return *input;
    }

    static bool match(string_view token, string_view str) {
        return token.size() == str.size()
            && strncasecmp(token.data(), str.data(), token.size()) == 0;
    }

    /// Skips whitespace, then reads & returns the next token:
    /// * an empty string at EOF;
    /// * a string literal, starting and ending with double-quotes;
    /// * a "{" or "}";
    /// * a "[" or "]";
    /// * else the largest number of consecutive non-whitespace non-closing-brace characters.
    static string_view readToken(const char* &input) {
        skipWhitespace(input);
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
            case '(':
            case '{':
            case '[':
                // Open array or quotation -- just return the single delimiter character
                ++input;
                break;
            default:
                // General token: read until next whitespace or closing brace/bracket:
                do {
                    ++input;
                } while (*input != 0 && !isspace(*input)
                         && *input != ')'&& *input != '}' && *input != ']');
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


    void Compiler::parse(const string &input) {
        const char *remainder = parse(input.c_str());
        if (*remainder != 0)
            throw compile_error("Unexpected delimiter; expected end of input", remainder);
    }


    const char* Compiler::parse(const char *input) {
        while (true) {
            string_view token = _curToken = readToken(input);
            const char *sourcePos = token.data();
            if (token.empty()) {
                // End of input
                break;

            } else if (token == "}") {
                // end of a nested word (quotation). Exit, but don't consume the '}'.
                --input;
                break;

            } else if (token[0] == '"') {
                // String literal:
                addLiteral(parseString(token), sourcePos);

            } else if (token == "[") {
                addLiteral(parseArray(input), token.data());

            } else if (token == "{") {
                addLiteral(parseQuote(input), token.data());

            } else if (match(token, "IF")) {
                // IF compiles into 0BRANCH, with offset TBD:
                pushBranch('i', &_ZBRANCH);

            } else if (match(token, "ELSE")) {
                // ELSE compiles into BRANCH, with offset TBD, and resolves the IF's branch:
                auto ifPos = popBranch("i");
                pushBranch('e', &_BRANCH);
                fixBranch(ifPos);

            } else if (match(token, "THEN")) {
                // THEN generates no code but completes the remaining branch from IF or ELSE:
                auto ifPos = popBranch("ie");
                fixBranch(ifPos);

            } else if (match(token, "BEGIN")) {
                // BEGIN generates no code but remembers the current address:
                pushBranch('b');

            } else if (match(token, "WHILE")) {
                // IF compiles into 0BRANCH, with offset TBD:
                if (_controlStack.empty() || _controlStack.back().first != 'b')
                    throw compile_error("no matching BEGIN for this WHILE", sourcePos);
                pushBranch('w', &_ZBRANCH);

            } else if (match(token, "REPEAT")) {
                // REPEAT generates a BRANCH back to the BEGIN's position,
                // and fixes up the WHILE to point to the next instruction:
                auto whilePos = popBranch("w");
                auto beginPos = popBranch("b");
                addBranchBackTo(beginPos);
                fixBranch(whilePos);

            } else if (match(token, "RECURSE")) {
                addRecurse();

            } else if (const Word *word = Compiler::activeVocabularies.lookup(token); word) {
                // Known word is added as an instruction:
                if (word->isMagic() || word->parameters() > 0)
                    throw compile_error("Special word " + string(word->name())
                                        + " cannot be added by parser", sourcePos);
                add(word, sourcePos);

            } else if (auto np = asNumber(token); np) {
                // A number is added as a LITERAL instruction:
                addLiteral(Value(*np), sourcePos);

            } else {
                throw compile_error("Unknown word '" + string(token) + "'", sourcePos);
            }
        }
        _curToken = {};
        return input;
    }


    Value Compiler::parseString(string_view token) {
        if (token.size() == 1 || token[token.size()-1] != '"')
            throw compile_error("Unfinished string literal", token.end());
        token = token.substr(1, token.size() - 2);
        return Value(token.data(), token.size());
    }


    Value Compiler::parseArray(const char* &input) {
        Value arrayVal({});
        std::vector<Value> *array = arrayVal.asArray();
        while (true) {
            string_view token = readToken(input);
            if (token == "]")
                break;
            else if (token.empty())
                throw compile_error("Unfinished array literal", input);
            else if (token[0] == '"')
                array->push_back(parseString(token));
            else if (token == "[")
                array->push_back(parseArray(input));
            else if (auto np = asNumber(token); np)
                array->push_back(Value(*np));
            else
                throw compile_error("Invalid literal '" + string(token) + "' in array", token.data());
        }
        return arrayVal;
    }


    Value Compiler::parseQuote(const char* &input) {
        Compiler quoteCompiler;
        // Check if there's a stack effect declaration:
        if (peek(input) == '(') {
            const char *start = input + 1;
            do {
                ++input;
                if (*input == 0)
                    throw compile_error("Missing ') to end quotation stack effect'", input);
            } while (*input != ')');
            quoteCompiler.setStackEffect(StackEffectParser().parse(start, input));
            ++input;
        }

        // parse tokens to a new Word until the ']' delimiter:
        input = quoteCompiler.parse(input);
        if (*input != '}')
            throw compile_error("Missing '}'; unfinished quotation", input);
        ++input;

        return Value(new CompiledWord(move(quoteCompiler)));
    }

}
