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
#include <initializer_list>
#include <optional>
#include <stdint.h>

namespace tails {

    /// A set of Value types. Describes one item, an input or output, in a word's stack effect.
    /// If it's a StackEffect output, it can optionally declare that it matches the type of an input.
    class TypeSet {
    public:
        constexpr TypeSet() { }

        constexpr TypeSet(Value::Type type)        {addType(type);}

        constexpr TypeSet(std::initializer_list<Value::Type> types) {
            for (auto type : types)
                addType(type);
        }

        constexpr static TypeSet anyType() {return TypeSet(kTypeFlags);}
        constexpr static TypeSet noType()  {return TypeSet();}

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

        constexpr void setInputMatch(TypeSet inputEntry, unsigned inputNo) {
            assert(inputNo <= 6);
            _flags = ((inputNo+1) << kNumTypes) | (inputEntry._flags & kTypeFlags);
        }

        constexpr TypeSet operator/ (unsigned inputNo) const {
            assert(inputNo <= 6);
            return TypeSet(typeFlags() | ((inputNo+1) << kNumTypes));
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

        constexpr bool operator== (const TypeSet &other) const   {return compare(other) == 0;}
        constexpr bool operator!= (const TypeSet &other) const   {return compare(other) != 0;}
        constexpr bool operator>  (const TypeSet &other) const   {return compare(other) > 0;}
        constexpr bool operator<  (const TypeSet &other) const   {return compare(other) < 0;}

        constexpr explicit operator bool() const                 {return exists();}

        constexpr TypeSet operator| (const TypeSet &other) const {return _flags | other._flags;}
        constexpr TypeSet operator& (const TypeSet &other) const {return _flags & other._flags;}
        constexpr TypeSet operator- (const TypeSet &other) const {return _flags & ~other._flags;}

        constexpr uint8_t typeFlags() const                      {return _flags & kTypeFlags;}
        constexpr uint8_t flags() const                          {return _flags;} // tests only

    private:
        constexpr TypeSet(int flags) :_flags(uint8_t(flags)) { }

        static constexpr int kNumTypes = 5;
        static constexpr uint8_t kTypeFlags = (1 << kNumTypes) - 1;

        uint8_t _flags = 0;
    };


    class StackEffect {
    public:
        /// Constructs an empty instance with zero inputs and outputs and max.
        constexpr StackEffect() { }

        /// Creates a stack effect from the number of inputs and outputs; any types are allowed.
        constexpr StackEffect(uint8_t inputs, uint8_t outputs)
        :StackEffect(inputs, outputs, 0)
        {
            setMax();
        }

        constexpr StackEffect(std::initializer_list<TypeSet> inputs,
                              std::initializer_list<TypeSet> outputs)
        :_ins(inputs.size())
        ,_outs(outputs.size())
        {
            if (inputs.size() + outputs.size() >= kMaxEntries)
                throw std::runtime_error("Too many stack entries");
            auto entry = &_entries[0];
            for (auto in : inputs)
                *entry++ = in;
            for (auto out : outputs)
                *entry++ = out;
            setMax();
        }

        /// Sets the max of an instance. Useful for compile-time definitions of interpreted words.
        constexpr StackEffect withMax(int max) {
            auto result = *this;
            result.setMax(max);
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
        friend constexpr void _parseStackEffect(StackEffect&, const char *str, const char *end);

        constexpr StackEffect(uint8_t inputs, uint8_t outputs, uint16_t max)
        :_ins(inputs), _outs(outputs), _max(max)
        {
            for (uint8_t i = 0; i < inputs + outputs; ++i)
                _entries[i] = TypeSet::anyType();
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

        constexpr void setMax(int m =0) {
            if (m > UINT16_MAX)
                throw std::runtime_error("Stack max too deep");
            m = std::max(m, net());
            _max = uint8_t(std::max(m, 0));
        }

        static constexpr size_t kMaxEntries = 8;
        using Entries = std::array<TypeSet, kMaxEntries>;

        Entries  _entries;              // Inputs (bottom to top), then outputs (same)
        uint8_t  _ins = 0, _outs = 0;   // Number of inputs and outputs
        uint16_t _max = 0;              // Max stack growth during run
        bool     _weird = false;        // If true, behavior not fixed at compile time
    };

}
