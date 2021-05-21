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
#include <algorithm>
#include <stdint.h>

namespace tails {

    /// Describes the effect upon the stack of a word.
    ///
    /// `in` is how many values it reads from the stack; `out` is how many it leaves behind.
    /// Thus its net effect on stack depth is `out - in`.
    ///
    /// `max` is the maximum depth of the stack while running the word, relative to when it starts.
    /// This can be used to allocate the minimally-sized stack when running a program.
    class StackEffect {
    public:
        /// Constructs an no-op empty StackEffect with no in or out.
        constexpr StackEffect()
        :_in(0), _net(0), _max(0) { }

        /// Constructs a StackEffect taking `input` inputs and `output` outputs.
        /// Its \ref max is assumed to be none beyond the outputs.
        constexpr StackEffect(uint8_t input, uint8_t output)
        :_in(input), _net(output - input), _max(std::max(_net, int8_t(0))) { }

        /// Constructs a StackEffect taking `input` inputs and `output` outputs,
        /// with a maximum stack depth (relative to its start) of `max`.
        constexpr StackEffect(uint8_t input, uint8_t output, uint16_t max)
        :_in(input), _net(output - input), _max(max) { }

        /// Number of items read from stack on entry (i.e. minimum stack depth on entry)
        constexpr int input() const     {return _in;}
        /// Number of items left on stack on exit, "replacing" the input
        constexpr int output() const    {return _in + _net;}
        /// Net change in stack depth from entry to exit; equal to `output` - `input`.
        constexpr int net() const       {return _net;}
        /// Max growth of stack while the word runs
        constexpr int max() const       {return _max;}

        constexpr bool operator== (const StackEffect &other) const {
            return _in == other._in && _net == other._net && _max == other._max;
        }

        constexpr bool operator!= (const StackEffect &other) const {return !(*this == other);}

        /// Returns the cumulative effect of two StackEffects, first `this` and then `next`.
        /// (The logic is complicated & confusing, since `next` gets offset by my `net`.)
        constexpr StackEffect then(const StackEffect &next) const {
            int in = std::max(this->input(),
                              next.input() - this->net());
            int net = this->net() + next.net();
            int max = std::max(this->max(),
                               next.max() + this->net());
            StackEffect result {uint8_t(in), uint8_t(in + net), uint16_t(max)};
            if (result._in != in || result._net != net || result._max != max)
                throw std::runtime_error("StackEffect overflow");
            return result;
        }

    private:
        uint8_t  _in;       // Minimum stack depth on entry (number of parameters)
        int8_t   _net;      // Change in stack depth on exit
        uint16_t _max;      // Maximum stack depth (relative to `_in`) while running
    };

}
