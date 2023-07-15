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
    class ROMStackEffect {
    public:
        //---- Constexpr API, for use defining the built-in primitive Words:

        /// Constructs an empty instance with zero inputs and outputs and max.
        constexpr ROMStackEffect() = default;

        /// Creates a stack effect from lists of inputs and outputs.
        constexpr ROMStackEffect(std::initializer_list<ROMTypeSet> inputs,
                                 std::initializer_list<ROMTypeSet> outputs)
        :_ins(inputs.size())
        ,_outs(outputs.size())
        {
            if (inputs.size() + outputs.size() >= kMaxSimpleEntries)
                throw std::runtime_error("Too many stack entries");
            auto entry = &_entries[0];
            for (auto in : inputs) {
                assert(in);
                *entry++ = in;
            }
            for (auto out : outputs) {
                assert(out);
                *entry++ = out;
            }
            setMax();
        }

        /// Returns a StackEffect whose inputs and outputs are not known at compile time.
        constexpr static ROMStackEffect weird() {
            ROMStackEffect result;
            result._weird = true;
            return result;
        }

        /// Number of items read from stack on entry (i.e. minimum stack depth on entry)
        constexpr int inputCount() const            {assert(!_weird); return _ins;}

        /// Number of items left on stack on exit, "replacing" the input
        constexpr int outputCount() const           {assert(!_weird); return _outs;}

        /// Net change in stack depth from entry to exit; equal to `outputCount` - `inputCount`.
        constexpr int net() const         {assert(!_weird); return int(_outs)-int(_ins);}

        /// Max growth of stack while the word runs.
        constexpr int max() const                   {assert(!_weird); return _max;}

        /// True if actual max stack growth is not known at compile time (e.g. recursive fns)
        constexpr bool maxIsUnknown() const         {return _max == UINT16_MAX;}

        /// True if the stack effect is unknown at compile time or depends on instruction params.
        constexpr bool isWeird() const              {return _weird;}

        static constexpr uint16_t kUnknownMax = UINT16_MAX;

    protected:
        friend class StackEffect;

        ROMStackEffect(size_t ins, size_t outs) :_ins(ins), _outs(outs) { }

        constexpr void checkNotFull() const {
            if (_ins + _outs >= kMaxSimpleEntries)
                throw std::runtime_error("Too many stack entries");
        }

        constexpr void setMax(int m =0) {
            m = std::max({m, 0, net(), int(_max)});
            _max = uint16_t(std::min(m, int(kUnknownMax)));
        }

        static constexpr size_t kMaxSimpleEntries = 8;
        using SimpleEntries = std::array<ROMTypeSet, kMaxSimpleEntries>;

        SimpleEntries  _entries;        // Inputs (bottom to top), then outputs (same)
        uint8_t  _ins = 0, _outs = 0;   // Number of inputs and outputs
        uint16_t _max = 0;              // Max stack growth during run
        bool     _weird = false;        // If true, behavior not fixed at compile time
        bool     _rom   = true;
    };



    class StackEffect : public ROMStackEffect {
    public:

        /// Constructs an empty instance with zero inputs and outputs and max.
        constexpr StackEffect() {_rom = false;}

        StackEffect(ROMStackEffect const& sfx)
        {
            if (sfx._rom) {
                _rom = false;
                for (uint8_t i = 0; i < sfx._ins; ++i)
                    addInput(sfx._entries[i]);
                for (uint8_t i = 0; i < sfx._outs; ++i)
                    addOutput(sfx._entries[sfx._ins + i]);
                _max = sfx._max;
                _weird = sfx._weird;
            } else {
                *this = (StackEffect const&)sfx;
            }
        }

        StackEffect(std::vector<TypeSet> const& inputs,
                    std::vector<TypeSet> const& outputs)
        :ROMStackEffect(inputs.size(), outputs.size())
        ,_inputs(inputs)
        ,_outputs(outputs)
        {
            _rom = false;
            for (auto &ts : _inputs) assert(ts);
            for (auto &ts : _outputs) assert(ts);
            setMax();
        }

        /// Returns a copy with the max stack depth set to `max`.
        /// (However, the max will not be set less than 0, or less than the `net()`.)
        /// Usually called after the constructor to declare a custom max.
        StackEffect withMax(int max) {
            auto result = *this;
            result.setMax(max);
            return result;
        }

        /// Returns a copy with the max stack depth set to "unknown".
        StackEffect withUnknownMax()      {return withMax(kUnknownMax);}

        /// The array of input types. Indexed with top of stack at 0.
        std::vector<TypeSet> const& inputs() const    {return _inputs;}
        std::vector<TypeSet> const& outputs() const   {return _outputs;}

        std::vector<TypeSet>& inputs()    {return _inputs;}
        std::vector<TypeSet>& outputs()   {return _outputs;}

        /// Adds an input at the top of the stack.
        void addInput(TypeSet const& entry) {
            assert(entry);
            _inputs.insert(_inputs.begin(), entry);
            _ins++;
            setMax();
        }

        /// Adds an output at the top of the stack.
        void addOutput(TypeSet const& entry) {
            assert(entry);
            _outputs.insert(_outputs.begin(), entry);
            _outs++;
            setMax();
        }

        /// Adds an input at the bottom of the stack.
        void addInputAtBottom(TypeSet const& entry) {
            assert(entry);
            _inputs.push_back(entry);
            ++_ins;
            setMax();
        }

        /// Adds an output at the bottom of the stack.
        void addOutputAtBottom(TypeSet const& entry) {
            assert(entry);
            _outputs.push_back(entry);
            ++_outs;
            setMax();
        }

        /// Removes all the outputs.
        void clearOutputs()                           {_outputs.clear(); _outs = 0; setMax();}


        friend bool operator== (const StackEffect &a, const StackEffect &b) {
            return a._inputs == b._inputs && a._outputs == b._outputs
                && a._max == b._max && !a._weird && !b._weird;
        }

        /// The result of concatenating effects `a` and `b`.
        /// - b cannot have more inputs than a has outputs.
        /// - the outputs of a have to be type-compatible with corresponding inputs of b.
        friend StackEffect operator | (StackEffect const& a, StackEffect const& b) {
            assert(!a._weird);
            assert(!b._weird);
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
        std::vector<TypeSet> _inputs, _outputs;
    };

}
