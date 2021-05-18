//
// word.hh
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
#include "instruction.hh"
#include <stdexcept>


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
        uint8_t  _in;       // Minimum stack depth on entry
        int8_t   _net;      // Change in stack depth on exit
        uint16_t _max;      // Maximum stack depth (relative to `_in`) while running
    };


    /// A Forth word definition: name, flags and code.
    /// This base class itself is used for predefined words that are constructed at compile time.
    /// The subclass \ref CompiledWord builds words at runtime.
    class Word {
    public:
        enum Flags : uint8_t {
            NoFlags     = 0,
            Native      = 1, ///< Implemented in native code (at `_instr.op`)
            HasIntParam = 2, ///< This word is followed by an integer param (BRANCH, 0BRANCH)
            HasValParam = 4, ///< This word is followed by a Value param (LITERAL)
            Magic       = 8, ///< Low-level, not allowed in parsed code (0BRANCH, CALL, etc.)
        };

        constexpr Word(const char *name, Op native, StackEffect effect, Flags flags =NoFlags)
        :_instr(native)
        ,_name(name)
        ,_effect(effect)
        ,_flags(Flags(flags | Native))
        { }

        constexpr Word(const char *name, StackEffect effect, const Instruction words[])
        :_instr(words)
        ,_name(name)
        ,_effect(effect)
        ,_flags(NoFlags)
        { }

        constexpr const char* name() const              {return _name;}
        constexpr Instruction instruction() const       {return _instr;}
        constexpr StackEffect stackEffect() const       {return _effect;}

        constexpr bool isNative() const                 {return (_flags & Native) != 0;}
        constexpr bool hasIntParam() const              {return (_flags & HasIntParam) != 0;}
        constexpr bool hasValParam() const              {return (_flags & HasValParam) != 0;}
        constexpr bool hasAnyParam() const              {return (_flags & (HasIntParam | HasValParam)) != 0;}
        constexpr bool isMagic() const                  {return (_flags & Magic) != 0;}

        constexpr operator Instruction() const          {return _instr;}

    protected:
        Word() :_instr {}, _name(nullptr), _effect(), _flags(NoFlags) { };

        Instruction _instr; // Instruction that calls it (either an Op or an Instruction*)
        const char* _name;  // Forth name, or NULL if anonymous
        StackEffect _effect;
        Flags       _flags; // Flags (see above)
    };


    constexpr inline bool operator==(const Word &a, const Word &b)
                                        {return a.instruction().native == b.instruction().native;}
    constexpr inline bool operator!=(const Word &a, const Word &b)
                                        {return !(a == b);}


    // Shortcut for defining a native word (see examples in core_words.cc.)
    // It should be followed by the C++ function body in curly braces.
    // The body can use parameters `sp` and `pc`, and should end by calling `NEXT()`.
    // @param NAME  The C++ name of the Word object to define.
    // @param FORTHNAME  The word's Forth name (a string literal.)
    // @param EFFECT  The \ref StackEffect. Must be accurate!
    // @param FLAGS  Flags; use \ref HasIntParam if this word takes a following parameter.
    #define NATIVE_WORD(NAME, FORTHNAME, EFFECT, FLAGS) \
        Value* f_##NAME(Value *sp, const Instruction *pc) noexcept; \
        constexpr Word NAME(FORTHNAME, f_##NAME, EFFECT, Word::Flags(FLAGS)); \
        Value* f_##NAME(Value *sp, const Instruction *pc) noexcept


    // Shortcut for defining a native word implementing a binary operator like `+` or `==`.
    // @param NAME  The C++ name of the Word object to define.
    // @param FORTHNAME  The word's Forth name (a string literal.)
    // @param INFIXOP  The raw C++ infix operator to implement, e.g. `+` or `==`.
    #define BINARY_OP_WORD(NAME, FORTHNAME, INFIXOP) \
        NATIVE_WORD(NAME, FORTHNAME, StackEffect(2,1), Word::NoFlags) { \
            sp[-1] = Value(sp[-1] INFIXOP sp[0]);\
            --sp;\
            NEXT(); \
        }


    // Shortcut for defining an interpreted word (see examples in core_words.cc.)
    // The variable arguments must be a list of previously-defined Word objects.
    // (A `RETURN` will be appended automatically.)
    // @param NAME  The C++ name of the Word object to define.
    // @param FORTHNAME  The word's Forth name (a string literal.)
    // @param EFFECT  The \ref StackEffect. Must be accurate!
    #define INTERP_WORD(NAME, FORTHNAME, EFFECT, ...) \
        static constexpr Instruction const i_##NAME[] { __VA_ARGS__, RETURN }; \
        constexpr Word NAME(FORTHNAME, EFFECT, i_##NAME)

}
