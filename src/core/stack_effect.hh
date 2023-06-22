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
#include "type_set.hh"
#include "value.hh"
#include "utils.hh"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <stdint.h>

namespace tails {

    /// Describes the API of a word:
    /// - how many inputs it reads from the stack, and their allowed types;
    /// - how many outputs it leaves on the stack, and their potential types;
    /// - the net change in stack depth (output count minus input count);
    /// - the maximum increase in stack depth during execution.
    /// This is used by the compiler's stack checker to verify stack safety and type safety,
    /// and by the interpreter to allocate a sufficiently large stack at runtime.
    class StackEffect {
    public:
        /// Constructs an empty instance with zero inputs and outputs and max.
        constexpr StackEffect() { }

        /// Creates a stack effect from lists of inputs and outputs.
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

        /// Creates a stack effect from inputs and outputs.
        constexpr StackEffect(TypesView inputs, TypesView outputs)
        :_ins(inputs.size())
        ,_outs(outputs.size())
        {
            if (inputs.size() + outputs.size() >= kMaxEntries)
                throw std::runtime_error("Too many stack entries");
            memcpy(&_entries[0],     inputs.rbegin(), _ins  * sizeof(TypeSet));
            memcpy(&_entries[_ins], outputs.rbegin(), _outs * sizeof(TypeSet));
            setMax();
        }

        /// Returns a copy with the max stack depth set to `max`.
        /// (However, the max will not be set less than 0, or less than the `net()`.)
        /// Usually called after the constructor to declare a custom max.
        constexpr StackEffect withMax(int max) {
            auto result = *this;
            result.setMax(max);
            return result;
        }

        static constexpr uint16_t kUnknownMax = UINT16_MAX;

        /// Returns a copy with the max stack depth set to "unknown".
        constexpr StackEffect withUnknownMax()      {return withMax(kUnknownMax);}

        /// Returns a StackEffect whose inputs and outputs are not known at compile time.
        constexpr static StackEffect weird() {
            StackEffect result;
            result._weird = true;
            return result;
        }

        /// Adds an input at the top of the stack.
        void addInput(TypeSet entry)                {insert(entry, _ins); ++_ins;}

        /// Adds an output at the top of the stack.
        void addOutput(TypeSet entry)               {insert(entry, _ins + _outs); ++_outs;}

        /// Adds an input at the bottom of the stack.
        void addInputAtBottom(TypeSet entry)        {insert(entry, 0); ++_ins;}

        /// Adds an output at the bottom of the stack.
        void addOutputAtBottom(TypeSet entry)       {insert(entry, _ins); ++_outs;}

        /// Removes all the outputs.
        void clearOutputs()                         {_outs = 0;}

        /// Number of items read from stack on entry (i.e. minimum stack depth on entry)
        constexpr int inputCount() const            {assert(!_weird); return _ins;}

        /// Number of items left on stack on exit, "replacing" the input
        constexpr int outputCount() const           {assert(!_weird); return _outs;}

        /// Net change in stack depth from entry to exit; equal to `outputCount` - `inputCount`.
        constexpr int net() const                   {assert(!_weird); return int(_outs)-int(_ins);}

        /// Max growth of stack while the word runs.
        constexpr int max() const                   {assert(!_weird); return _max;}

        /// True if actual max stack growth is not known at compile time (e.g. recursive fns)
        constexpr bool maxIsUnknown() const         {return _max == UINT16_MAX;}

        /// True if the stack effect is unknown at compile time or depends on instruction params.
        constexpr bool isWeird() const              {return _weird;}

        /// The array of input types. Indexed with top of stack at 0.
        constexpr const TypesView inputs() const    {return TypesView((TypeSet*)&_entries[0],_ins);}
        constexpr TypesView inputs()                {return TypesView(&_entries[0], _ins);}

        /// The array of output types. Indexed with top of stack at 0.
        constexpr const TypesView outputs() const   {return TypesView((TypeSet*)&_entries[_ins], _outs);}
        constexpr TypesView outputs()               {return TypesView(&_entries[_ins], _outs);}

        constexpr bool operator== (const StackEffect &other) const {
            if (_ins == other._ins && _outs == other._outs && _max == other._max
                    && !_weird && !other._weird) {
                for (int i = _ins + _outs - 1; i >= 0; --i)
                    if (_entries[i] != other._entries[i])
                        return false;
                return true;
            }
            return false;
        }

        /// The result of concatenating effects `a` and `b`.
        /// - b cannot have more inputs than a has outputs.
        /// - the outputs of a have to be type-compatible with corresponding inputs of b.
        friend StackEffect operator | (StackEffect const& a, StackEffect const& b) {
            auto aOutputs = a.outputCount(), bInputs = b.inputCount();
            if (aOutputs < bInputs)
                throw std::logic_error("stack underflow");
            for (int i = 0; i < bInputs; ++i) {
                if (TypeSet badTypes = a.outputs()[i] - b.inputs()[i]) {
                    //auto badType = badTypes.firstType();
                    throw std::logic_error("type mismatch");
                }
            }

            StackEffect result(a.inputs(), b.outputs());

            // Add unconsumed inputs of a as outputs, below b's outputs:
            for (int i = bInputs; i < aOutputs; ++i)
                result.addOutputAtBottom(a.outputs()[i]);

            // If any outputs match inputs, update their types:
            for (int i = 0; i < result.outputCount(); ++i) {
                if (auto &type = result.outputs()[i]; type.isInputMatch()) {
                    int inputNo = type.inputMatch();
                    auto inType = a.outputs()[inputNo];
                    if (inType.multiType())
                        type.setInputMatch(inType, inputNo);
                    else
                        type = inType;
                }
            }
            return result;
        }

    private:
        friend constexpr void _parseStackEffect(StackEffect&, const char *str, const char *end);

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
            m = std::max({m, 0, net(), int(_max)});
            _max = uint16_t(std::min(m, int(kUnknownMax)));
        }

        static constexpr size_t kMaxEntries = 8;
        using Entries = std::array<TypeSet, kMaxEntries>;

        Entries  _entries;              // Inputs (bottom to top), then outputs (same)
        uint8_t  _ins = 0, _outs = 0;   // Number of inputs and outputs
        uint16_t _max = 0;              // Max stack growth during run
        bool     _weird = false;        // If true, behavior not fixed at compile time
    };

}
