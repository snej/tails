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

#include "stack_effect.hh"

namespace tails {

    /// Compile-time stack-effect parser. This lets you represent stack effects as strings in
    /// Tails syntax, without incurring any parsing overhead; in fact the StackEffect object can
    /// be stored as-is in the program's binary.
    /// The most convenient way to use this is with the `_sfx` string literal suffix, e.g:
    /// `static constexpr StackEffect kEffect = "a# b# -- c#"_sfx;`


    // Adds a type to a TypeSet given its stack-effect symbol.
    constexpr void addTypeSymbol(TypeSet &ts, char symbol) {
        switch (symbol) {
            case '?':           ts.addType(Value::ANull); break;
            case '$':           ts.addType(Value::AString); break;
            case '[': case ']': ts.addType(Value::AnArray); break;
            case '{': case '}': ts.addType(Value::AQuote); break;
            case '#':           ts.addType(Value::ANumber); break;
            case 'a'...'z':
            case 'A'...'Z':
            case '0'...'9':
            case '_':           break;
            default:            throw std::runtime_error("Unknown stack type symbol");
        }
    }

    /// Creates a TypeSet from a token string:
    /// - Alphanumerics and `_` are ignored
    /// - `?` means a null
    /// - `#` means a number
    /// - `$` means a string
    /// - `{` or `}` means an array
    /// - `[` or `]` means a quotation
    /// - If more than one type is given, either is allowed.
    /// - If no types are given, or only null, then any type is allowed.
    constexpr TypeSet parseTypeSet(const char *token, const char *tokenEnd = nullptr) {
        TypeSet ts;
        while (token != tokenEnd && *token)
            addTypeSymbol(ts, *token++);
        if (!ts.exists() || ts.flags() == 1)
            ts.addAllTypes();
        return ts;
    }


    /// Initializes a StackEffect instance from a human-readable stack effect declaration.
    /// Generally you call \ref parseStackEffect instead, which returns a new instance.
    /// - Each token before the `--` is an input, each one after is an output.
    /// - Punctuation marks in tokens denote types, as described in the \ref TypeSet constructor;
    ///   alphanumeric characters don't imply a type. If no type is given, any type is allowed.
    /// - If an output token exactly matches an input, and contains alphanumerics, that means it
    ///   has the same type as that input. So output "x" matches input "x". Output "n#?" matches
    ///   input "n#?" but not "n#". Output "#" can't match anything.
    constexpr void _parseStackEffect(StackEffect &effect, const char *str, const char *end) {
        const char* tokenStart[StackEffect::kMaxEntries] = {};  // Start of each token in `str`
        size_t tokenLen[ StackEffect::kMaxEntries] = {};        // Length of each token in `str`
        auto entry = effect._entries.begin();                   // Current TypeSet being populated
        bool inputs = true;                                     // Are we still parsing inputs?
        const char *token = nullptr;                            // Current token, or NULL
        bool tokenIsNamed = false;                              // Does token have alphanumerics?

        for (const char *c = str; c <= end; ++c) {
            if (c == end || *c == 0 || *c == ' ' || *c == '\t') {
                if (token) {
                    // End of token:
                    if (!entry->exists() || entry->flags() == 0x1)
                        entry->addAllTypes();
                    if (inputs) {
                        if (tokenIsNamed) {
                            tokenStart[effect._ins] = token;
                            tokenLen[effect._ins] = c - token;
                        }
                        ++effect._ins;
                    } else {
                        // look for input token match:
                        if (tokenIsNamed) {
                            for (unsigned b = 0; b < effect._ins; b++) {
                                if (tokenLen[b] == (c - token)
                                        && _compare(tokenStart[b], token, tokenLen[b])) {
                                    entry->setInputMatch(effect._entries[b], effect._ins - 1 - b);
                                    break;
                                }
                            }
                        }
                        ++effect._outs;
                    }
                    ++entry;
                    token = nullptr;
                    tokenIsNamed = false;
                }
            } else if (*c == '-') {
                // Separator:
                if (c+1 == end || c[1] != '-' || token || !inputs)
                    throw std::runtime_error("Invalid stack separator");
                c += 2;
                inputs = false;
            } else {
                if (!token) {
                    // Start of token:
                    effect.checkNotFull();
                    token = c;
                }
                // Add character to token:
                addTypeSymbol(*entry, *c);
                if (_isalpha(*c))
                    tokenIsNamed = true;
            }
        }
        if (inputs)
            throw std::runtime_error("Missing stack separator");
        effect.setMax();
    }

    /// Creates a StackEffect from a human-readable stack effect declaration.
    /// See the above \ref _parseStackEffect function for details.
    constexpr StackEffect parseStackEffect(const char *str, const char *end) {
        StackEffect effect;
        _parseStackEffect(effect, str, end);
        return effect;
    }


    /// Creates a StackEffect from a human-readable stack effect declaration.
    /// See the above \ref _parseStackEffect function for details.
    constexpr StackEffect parseStackEffect(const char *str) {
        return parseStackEffect(str, str + _strlen(str));
    }


    /// Special operator that lets you create a StackEffect by suffixing its string literal form
    /// with `_sfx`.
    constexpr static inline StackEffect operator""_sfx (const char *str, size_t len) {
        StackEffect effect;
        _parseStackEffect(effect, str, str + len);
        return effect;
    }
}
