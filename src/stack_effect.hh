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
#include <algorithm>
#include <array>
#include <ctype.h>
#include <stdint.h>

namespace tails {

    /// Describes one item, an input or output, in a word's stack effect.
    /// Specifies possible Value types, and if it's an output, whether it matches an input.
    class StackEffectEntry {
    public:
        constexpr StackEffectEntry() { }

        /// Constructs a stack effect entry from a token.
        /// - Alphanumerics and `_` are ignored
        /// - `?` means a null
        /// - `#` means a number
        /// - `$` means a string
        /// - `{` or `}` means an array
        /// - `[` or `]` means a quotation
        /// - If more than one type is given, either is allowed.
        /// - If no types are given or only null, then any type is allowed.
        explicit constexpr StackEffectEntry(const char *token, const char *tokenEnd = nullptr) {
            while (token != tokenEnd && *token)
                addTypeSymbol(*token++);
            if (_flags == 0 || _flags == 1)
                _flags = 0x1F;
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
        constexpr bool canBeType(Value::Type type) const    {return (_flags & (1 << int(type))) != 0;}
        constexpr bool isInputMatch() const                 {return (_flags & 0xE0) != 0;}
        constexpr int inputMatch() const                 {return (_flags >> 5) - 1;}

        constexpr void addType(Value::Type type)    {_flags |= (1 << int(type));}
        constexpr void addAllTypes()                {_flags = 0x1F;}

        constexpr void setInputMatch(StackEffectEntry inputEntry, unsigned entryNo) {
            _flags = ((entryNo+1) << 5) | (inputEntry._flags & 0x1F);
        }

        /// I am greater than another entry if I support types it doesn't.
        constexpr bool operator> (const StackEffectEntry &other) const {
            return (typeFlags() & ~other.typeFlags()) != 0;
        }

        constexpr uint8_t typeFlags() const                     {return _flags & 0x1F;}
        constexpr uint8_t flags() const                         {return _flags;} // tests only

    private:
        uint8_t _flags = 0;
    };


    class StackEffect {
    public:
        /// Constructs an empty instance with zero inputs and outputs and max.
        constexpr StackEffect() { }

        /// Constructs an instance from a human-readable stack effect declaration.
        /// - Each token before the `--` is an input, each one after is an output.
        /// - Tokens denote types, as described in the \ref StackEffectEntry constructor.
        /// - Outputs whose names exactly match inputs denote the same exact value at runtime.
        constexpr StackEffect(const char *str, const char *end) {
            const char *tokenStart[kNumEntries] = {};
            size_t tokenLen[kNumEntries] = {};
            auto entry = _entries.begin();
            bool inputs = true;
            const char *token = nullptr;

            for (const char *c = str; c <= end; ++c) {
                if (c == end || *c == 0 || *c == ' ' || *c == '\t') {
                    if (token) {
                        // End of token:
                        if (!entry->exists() || entry->flags() == 0x1)
                            entry->addAllTypes();
                        if (inputs) {
                            tokenStart[_ins] = token;
                            tokenLen[_ins] = c - token;
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
                        if (_ins + _outs >= kNumEntries)
                            throw std::runtime_error("Too many stack entries");
                        token = c;
                    }
                    // Symbol in token:
                    entry->addTypeSymbol(*c);
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

        constexpr StackEffectEntry input(unsigned i) const  {assert(i < _ins);  return _entries[_ins - 1 - i];}
        constexpr StackEffectEntry output(unsigned i) const {assert(i < _outs); return _entries[_ins + _outs - 1 - i];}

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

        //---- COMBINING:

        /// Returns the cumulative effect of two StackEffects, first `this` and then `next`.
        /// (The logic is complicated & confusing, since `next` gets offset by my `net`.)
        constexpr StackEffect then(const StackEffect &next) const {
            // FIXME: TODO: This does not check types, nor propagate them to the result
            int in = std::max(this->inputs(),
                              next.inputs() - this->net());
            int net = this->net() + next.net();
            int max = std::max(this->max(),
                               next.max() + this->net());
            StackEffect result {uint8_t(in), uint8_t(in + net), uint16_t(max)};
            if (result._ins != in || result.net() != net || result._max != max)
                throw std::runtime_error("StackEffect overflow");
            return result;
        }

        /// Returns the effect of doing _either_ of `this` or `other` (like branches of an 'IF'.)
        /// They must have the same `net`.
        /// The result will have the maximum of their `input`s, `output`s and `max`s.
        StackEffect either(const StackEffect &other) {
            // FIXME: TODO: This does not check types, nor propagate them to the result
            assert(this->net() == other.net());
            int in  = std::max(this->inputs(), other.inputs());
            int max = std::max(this->max(), other.max());
            return {uint8_t(in), uint8_t(in + this->net()), uint16_t(max)};
        }

    private:
        constexpr StackEffect(uint8_t inputs, uint8_t outputs, uint16_t max)
        :_ins(inputs), _outs(outputs), _max(max)
        {
            for (uint8_t i = 0; i < inputs + outputs; ++i)
                _entries[i] = StackEffectEntry("a");
        }

        static constexpr size_t _strlen(const char *str) noexcept {
            if (!str)
                return 0;
            auto c = str;
            while (*c) ++c;
            return c - str;
        }

        static constexpr bool _compare(const char *a, const char *b, size_t len) {
            while (len-- > 0)
                if (*a++ != *b++)
                    return false;
            return true;
        }

        static constexpr size_t kNumEntries = 8;
        using StackEntries = std::array<StackEffectEntry, kNumEntries>;

        StackEntries _entries;
        uint8_t      _ins = 0, _outs = 0, _max = 0;
        bool         _weird = false;
    };


#if 0
    class EffectStack {
    public:
        EffectStack() { }

        EffectStack(const TypedStackEffect &initial) {
            for (int i = 0; i < initial.inputs(); ++i)
                _stack.push_back({initial.input(i), nullptr});
        }

        struct Item {
            StackEffectEntry entry;
            std::optional<Value> value;
        };

        size_t depth() const {return _stack.size();}

        const Item& at(unsigned i) const {return _stack[_stack.size() - 1 - i];}

        bool add(const TypedStackEffect &effect) {
            if (effect.inputs() > depth())
                return false;
            for (int i = 0; i < effect.inputs(); ++i) {
                auto ef = effect.input(i);
                auto &item = at(i);
                if (item.value) {
                    if (!ef.canBeType(item.value->type()))
                        return false;
                } else {
                    if (item.entry > ef)
                        return false;
                }
            }
            for (int i = 0; i < effect.outputs(); ++i) {
                auto ef = effect.output(i);
                •••
            }
        }

    private:
        std::vector<Item> _stack;
    };
#endif

}
