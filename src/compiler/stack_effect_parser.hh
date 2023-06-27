//
// stack_effect_parser.hh
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

#pragma once
#include "stack_effect.hh"

namespace tails {

    /// Compile-time stack-effect parser. This lets you represent stack effects as strings in
    /// Tails syntax, without incurring any parsing overhead; in fact the StackEffect object can
    /// be stored as-is in the program's binary.
    /// The most convenient way to use this is with the `_sfx` string literal suffix, e.g:
    /// `static constexpr StackEffect kEffect = "a# b# -- c#"_sfx;`
    class StackEffectParser {
    public:
        StackEffectParser() = default;

        /// Creates a StackEffect from a human-readable stack effect declaration.
        /// See the above \ref _parseStackEffect function for details.
        constexpr StackEffect parse(const char *str, const char *end) {
            _parseStackEffect(str, end);
            return effect;
        }


        /// Creates a StackEffect from a human-readable stack effect declaration.
        /// See the above \ref _parseStackEffect function for details.
        constexpr StackEffect parse(const char *str) {
            return parse(str, str + _strlen(str));
        }

        /// After parsing, the resulting StackEffect.
        StackEffect effect;

        /// After parsing, the names given to inputs/outputs, or empty strings if none.
        std::vector<std::string_view> inputNames, outputNames;

    private:
        // Adds a type to a TypeSet given its stack-effect symbol.
        static constexpr void addTypeSymbol(TypeSet &ts, char const* symbol) {
            switch (*symbol) {
                case '?':           ts.addType(Value::ANull); break;
                case '$':           ts.addType(Value::AString); break;
                case '[': case ']': ts.addType(Value::AnArray); break;
                case '{': case '}': ts.addType(Value::AQuote); break;
                case '#':           ts.addType(Value::ANumber); break;
                case 'a'...'z':
                case 'A'...'Z':
                case '0'...'9':
                case '_':           break;
                default:            throw compile_error("Unknown stack type symbol", symbol);
            }
        }
        
        /// Creates a TypeSet from a token string:
        /// - Consecutive alphanumerics and `_` form the parameter name. If an output has the
        ///   same name as an input, that means its runtime type will be the same as the input's.
        /// - `?` means a null
        /// - `#` means a number
        /// - `$` means a string
        /// - `{` or `}` means an array
        /// - `[` or `]` means a quotation
        /// - If more than one type is given, either is allowed.
        /// - If no types are given, or only null, then any type is allowed.
        static constexpr TypeSet parseTypeSet(const char *token, const char *tokenEnd = nullptr) {
            TypeSet ts;
            while (token != tokenEnd && *token)
                addTypeSymbol(ts, token++);
            if (!ts.exists() || ts.flags() == 1)
                ts.addAllTypes();
            return ts;
        }
        
        
        /// Initializes a StackEffect instance from a human-readable stack effect declaration.
        /// - Each token before the `--` is an input, each one after is an output.
        /// - Punctuation marks in tokens denote types, as described in the \ref TypeSet constructor;
        ///   alphanumeric characters don't imply a type. If no type is given, any type is allowed.
        /// - If an output token exactly matches an input, and contains alphanumerics, that means it
        ///   has the same type as that input. So output "x" matches input "x". Output "n#?" matches
        ///   input "n#?" but not "n#". Output "#" can't match anything.
        constexpr void _parseStackEffect(const char *str, const char *end) {
            effect = StackEffect();
            inputNames.clear();
            outputNames.clear();

            auto entry = effect._entries.begin();                   // Current TypeSet being populated
            bool inputs = true;                                     // Are we still parsing inputs?
            const char *token = nullptr;                            // Current token, or NULL
            const char *tokenNameBegin = nullptr;                              // Does token have alphanumerics?
            const char *tokenNameEnd = nullptr;

            for (const char *c = str; c <= end; ++c) {
                if (c == end || *c == 0 || *c == ' ' || *c == '\t') {
                    if (token) {
                        // End of token:
                        if (!entry->exists() || entry->flags() == 0x1)
                            entry->addAllTypes();
                        std::string_view name;
                        if (tokenNameBegin) {
                            if (!tokenNameEnd)
                                tokenNameEnd = c;
                            name = std::string_view(tokenNameBegin, tokenNameEnd);
                        }
                        if (inputs) {
                            inputNames.push_back(name);
                            ++effect._ins;
                        } else {
                            // look for input token match:
                            if (tokenNameBegin) {
                                if (auto i = std::find(inputNames.begin(), inputNames.end(), name); i != inputNames.end()) {
                                    name = *i;
                                    auto inputNo = (inputNames.size() - 1) - (i - inputNames.begin());
                                    entry->setInputMatch(effect._entries[inputNo], unsigned(inputNo));
                                }
                            }
                            outputNames.push_back(name);
                            ++effect._outs;
                        }
                        ++entry;
                        token = nullptr;
                        tokenNameBegin = tokenNameEnd = nullptr;
                    }
                } else if (*c == '-') {
                    // Separator:
                    if (c+1 == end || c[1] != '-' || token || !inputs)
                        throw compile_error("Invalid stack separator", c);
                    c += 2;
                    inputs = false;
                } else {
                    if (!token) {
                        // Start of token:
                        effect.checkNotFull();
                        token = c;
                    }
                    // Add character to token:
                    addTypeSymbol(*entry, c);
                    if (_isalpha(*c) || *c == '_') {
                        if (!tokenNameBegin)
                            tokenNameBegin = c;
                        if (tokenNameEnd)
                            throw compile_error("Invalid parameter or result name", c);
                    } else if (tokenNameBegin) {
                        tokenNameEnd = c;
                    }
                }
            }
            if (inputs)
                throw compile_error("Missing stack separator", end);
            effect.setMax();
            std::reverse(inputNames.begin(), inputNames.end());
            std::reverse(outputNames.begin(), outputNames.end());
        }
    };


    /// Special operator that lets you create a StackEffect by suffixing its string literal form
    /// with `_sfx`.
    constexpr static inline StackEffect operator""_sfx (const char *str, size_t len) {
        return StackEffectParser().parse(str, str + len);
    }
}
