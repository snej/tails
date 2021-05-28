//
// stack_effect.hh
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
#include "value.hh"
#include "utils.hh"
#include <algorithm>
#include <array>
#include <ctype.h>
#include <optional>
#include <stdint.h>

namespace tails {

    /// A set of Value types. Describes one item, an input or output, in a word's stack effect.
    /// If it's a StackEffect output, it can optionally declare that it matches the type of an input.
    class TypeSet {
    public:
        constexpr TypeSet() { }

        constexpr explicit TypeSet(Value::Type type) {
            addType(type);
        }

        constexpr static TypeSet anyType() {return TypeSet(kTypeFlags);}
        constexpr static TypeSet noType() {return TypeSet();}

        /// Constructs a TypeSet from a token:
        /// - Alphanumerics and `_` are ignored
        /// - `?` means a null
        /// - `#` means a number
        /// - `$` means a string
        /// - `{` or `}` means an array
        /// - `[` or `]` means a quotation
        /// - If more than one type is given, either is allowed.
        /// - If no types are given, or only null, then any type is allowed.
        explicit constexpr TypeSet(const char *token, const char *tokenEnd = nullptr) {
            while (token != tokenEnd && *token)
                addTypeSymbol(*token++);
            if (_flags == 0 || _flags == 1)
                _flags = kTypeFlags;
        }

        constexpr void addTypeSymbol(char symbol) {
            switch (symbol) {
                case '?':           addType(Value::ANull); break;
                case '#':           addType(Value::ANumber); break;
                case '$':           addType(Value::AString); break;
                case '{': case '}': addType(Value::AnArray); break;
                case '[': case ']': addType(Value::AQuote); break;
                case 'a'...'z':
                case 'A'...'Z':
                case '0'...'9':
                case '_':           break;
                default:            throw std::runtime_error("Unknown stack type symbol");
            }
        }

        constexpr bool exists() const                       {return _flags != 0;}
        constexpr bool canBeAnyType() const                 {return typeFlags() == kTypeFlags;}
        constexpr bool canBeType(Value::Type type) const    {return (_flags & (1 << int(type))) != 0;}

        std::optional<Value::Type> firstType() const {
            for (int i = 0; i < kNumTypes; ++i)
                if (_flags & (1<<i))
                    return Value::Type(i);
            return std::nullopt;
        }

        constexpr void addType(Value::Type type)            {_flags |= (1 << int(type));}
        constexpr void addAllTypes()                        {_flags = kTypeFlags;}

        constexpr bool isInputMatch() const                 {return (_flags & 0xE0) != 0;}
        constexpr int inputMatch() const                    {return (_flags >> kNumTypes) - 1;}

        constexpr void setInputMatch(TypeSet inputEntry, unsigned entryNo) {
            _flags = ((entryNo+1) << kNumTypes) | (inputEntry._flags & kTypeFlags);
        }

        /// I am "greater than" another entry if I support types it doesn't.
        constexpr int compare(const TypeSet &other) const {
            if (typeFlags() == other.typeFlags())
                return 0;
            else if ((typeFlags() & ~other.typeFlags()) != 0)
                return 1;
            else
                return -1;
        }

        constexpr bool operator== (const TypeSet &other) const {return compare(other) == 0;}
        constexpr bool operator!= (const TypeSet &other) const {return compare(other) != 0;}
        constexpr bool operator> (const TypeSet &other) const {return compare(other) > 0;}
        constexpr bool operator< (const TypeSet &other) const {return compare(other) < 0;}

        constexpr explicit operator bool() const                {return exists();}

        constexpr TypeSet operator| (const TypeSet &other) const {
            return TypeSet((_flags | other._flags) & kTypeFlags);
        }

        constexpr TypeSet operator& (const TypeSet &other) const {
            return TypeSet((_flags & other._flags) & kTypeFlags);
        }

        constexpr TypeSet operator- (const TypeSet &other) const {
            return TypeSet((_flags & ~other._flags) & kTypeFlags);
        }

        constexpr uint8_t typeFlags() const                     {return _flags & kTypeFlags;}
        constexpr uint8_t flags() const                         {return _flags;} // tests only

    private:
        constexpr explicit TypeSet(uint8_t flags) :_flags(flags) { }

        static constexpr int kNumTypes = 5;
        static constexpr uint8_t kTypeFlags = (1 << kNumTypes) - 1;

        uint8_t _flags = 0;
    };


    class StackEffect {
    public:
        /// Constructs an empty instance with zero inputs and outputs and max.
        constexpr StackEffect() { }

        /// Constructs an instance from a human-readable stack effect declaration.
        /// - Each token before the `--` is an input, each one after is an output.
        /// - Tokens denote types, as described in the \ref TypeSet constructor.
        /// - Outputs whose names exactly match inputs denote the same exact value at runtime.
        constexpr StackEffect(const char *str, const char *end) {
            const char *tokenStart[kMaxEntries] = {};
            size_t tokenLen[kMaxEntries] = {};
            auto entry = _entries.begin();
            bool inputs = true;
            const char *token = nullptr;
            bool tokenIsNamed = false;

            for (const char *c = str; c <= end; ++c) {
                if (c == end || *c == 0 || *c == ' ' || *c == '\t') {
                    if (token) {
                        // End of token:
                        if (!entry->exists() || entry->flags() == 0x1)
                            entry->addAllTypes();
                        if (inputs) {
                            if (tokenIsNamed) {
                                tokenStart[_ins] = token;
                                tokenLen[_ins] = c - token;
                            }
                            ++_ins;
                        } else {
                            // look for 'before' token match:
                            for (unsigned b = 0; b < _ins; b++) {
                                if (tokenLen[b] == (c - token) && _compare(tokenStart[b], token, tokenLen[b])) {
                                    entry->setInputMatch(_entries[b], _ins - 1 - b);
                                    break;
                                }
                            }
                            ++_outs;
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
                        checkNotFull();
                        token = c;
                    }
                    // Symbol in token:
                    entry->addTypeSymbol(*c);
                    if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z'))
                        tokenIsNamed = true;
                }
            }
            if (inputs)
                throw std::runtime_error("Missing stack separator");
            _max = uint8_t(std::max(net(), 0));
        }

        constexpr StackEffect(const char *str) :StackEffect(str, str + _strlen(str)) { }

        /// Creates a stack effect from the number of inputs and outputs; any types are allowed.
        constexpr StackEffect(uint8_t inputs, uint8_t outputs)
        :StackEffect(inputs, outputs, 0)
        {
            _max = uint8_t(std::max(net(), 0));
        }

        /// Sets the max of an instance. Useful for compile-time definitions of interpreted words.
        constexpr StackEffect withMax(int max) {
            auto result = *this;
            result._max = uint8_t(std::min(255, std::max(0, std::max(max, net()))));
            return result;
        }

        /// Returns a StackEffect whose inputs and outputs are not known at compile time.
        constexpr static StackEffect weird() {
            StackEffect result;
            result._weird = true;
            return result;
        }

        void addInput(TypeSet entry) {
            insert(entry, _ins);
            ++_ins;
        }

        void addOutput(TypeSet entry) {
            insert(entry, _ins + _outs);
            ++_outs;
        }

        void addInputAtBottom(TypeSet entry) {
            insert(entry, 0);
            ++_ins;
        }

        void addOutputAtBottom(TypeSet entry) {
            insert(entry, _ins);
            ++_outs;
        }

        /// Number of items read from stack on entry (i.e. minimum stack depth on entry)
        constexpr int inputs() const    {assert(!_weird); return _ins;}
        /// Number of items left on stack on exit, "replacing" the input
        constexpr int outputs() const   {assert(!_weird); return _outs;}
        /// Net change in stack depth from entry to exit; equal to `output` - `input`.
        constexpr int net() const       {assert(!_weird); return int(_outs) - int(_ins);}
        /// Max growth of stack while the word runs
        constexpr int max() const       {assert(!_weird); return _max;}
        /// True if the stack effect is unknown at compile time or depends on instruction params
        constexpr bool isWeird() const  {return _weird;}

        constexpr TypeSet& input(unsigned i)  {assert(i < _ins);  return _entries[_ins - 1 - i];}
        constexpr TypeSet& output(unsigned i) {assert(i < _outs); return _entries[_ins + _outs - 1 - i];}

        constexpr TypeSet input(unsigned i) const  {assert(i < _ins);  return _entries[_ins - 1 - i];}
        constexpr TypeSet output(unsigned i) const {assert(i < _outs); return _entries[_ins + _outs - 1 - i];}

        constexpr bool operator== (const StackEffect &other) const {
            if (_ins == other._ins && _outs == other._outs && _max == other._max
                    && !_weird && !other._weird) {
                for (int i = _ins + _outs - 1; i >= 0; --i)
                    if (_entries[i].flags() != other._entries[i].flags())
                        return false;
                return true;
            }
            return false;
        }

        constexpr bool operator!= (const StackEffect &other) const {return !(*this == other);}

    private:
        constexpr StackEffect(uint8_t inputs, uint8_t outputs, uint16_t max)
        :_ins(inputs), _outs(outputs), _max(max)
        {
            for (uint8_t i = 0; i < inputs + outputs; ++i)
                _entries[i] = TypeSet("a");
        }

        constexpr void checkNotFull() const {
            if (_ins + _outs >= kMaxEntries)
                throw std::runtime_error("Too many stack entries");
        }

        void insert(TypeSet entry, int index) {
            assert(entry);
            checkNotFull();
            std::copy_backward(&_entries[index], &_entries[_ins + _outs], &_entries[_ins + _outs + 1]);
            _entries[index] = entry;
        }

        static constexpr size_t kMaxEntries = 8;
        using Entries = std::array<TypeSet, kMaxEntries>;

        Entries _entries;              // Inputs (bottom to top), then outputs (same)
        uint8_t _ins = 0, _outs = 0;   // Number of inputs and outputs
        uint8_t _max = 0;              // Max stack growth during run
        bool    _weird = false;        // If true, behavior not fixed at compile time
    };

}
