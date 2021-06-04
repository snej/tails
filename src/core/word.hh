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
#include "stack_effect.hh"
#include <stdexcept>


namespace tails {



    /// A Forth word definition: name, flags and code.
    /// This base class itself is used for predefined words that are constructed at compile time.
    /// The subclass \ref CompiledWord builds words at runtime.
    class Word {
    public:
        enum Flags : uint8_t {
            NoFlags     = 0x00,
            Native      = 0x01, ///< Implemented in native code (at `_instr.op`)
            HasIntParam = 0x02, ///< This word is followed by an integer param (BRANCH, 0BRANCH)
            HasValParam = 0x04, ///< This word is followed by a Value param (LITERAL)
            HasWordParam= 0x08, ///< This word is followed by a Word param (INTERP, TAILINTERP, etc)
            Magic       = 0x10, ///< Low-level, not allowed in parsed code (0BRANCH, INTERP, etc.)
            Inline      = 0x20, ///< Should be inlined at call site

            MagicIntParam  = Magic | HasIntParam,
            MagicValParam  = Magic | HasValParam,
            MagicWordParam = Magic | HasWordParam,
        };

        constexpr Word(const char *name,
                       Op native,
                       StackEffect effect,
                       Flags flags =NoFlags,
                       uint8_t nParams =0)
        :_instr(native)
        ,_name(name)
        ,_effect(effect)
        ,_flags(Flags(flags | Native))
        ,_nParams(std::max(nParams, uint8_t((flags & (HasIntParam|HasValParam|HasWordParam)) != 0)))
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

        constexpr bool hasFlag(Flags f) const           {return (_flags & f) != 0;}
        constexpr bool isNative() const                 {return hasFlag(Native);}
        constexpr uint8_t parameters() const            {return _nParams;}
        constexpr bool hasIntParams() const             {return hasFlag(HasIntParam);}
        constexpr bool hasValParams() const             {return hasFlag(HasValParam);}
        constexpr bool hasWordParams() const            {return hasFlag(HasWordParam);}
        constexpr bool isMagic() const                  {return hasFlag(Magic);}

        constexpr operator Instruction() const          {return _instr;}

    protected:
        Word() :_instr {}, _name(nullptr), _effect(), _flags(NoFlags) { };

        Instruction _instr; // Instruction that calls it (either an Op or an Instruction*)
        const char* _name;  // Forth name, or NULL if anonymous
        StackEffect _effect;
        Flags       _flags; // Flags (see above)
        uint8_t     _nParams = 0;
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
    // Flags and parameter count may optionally follow, as per the Word constructor.
    #define NATIVE_WORD(NAME, FORTHNAME, EFFECT, ...) \
        extern "C" Value* f_##NAME(Value *sp, const Instruction *pc); \
        constexpr Word NAME(FORTHNAME, f_##NAME, EFFECT, ## __VA_ARGS__); \
        Value* f_##NAME(Value *sp, const Instruction *pc)


    // Shortcut for defining a native word implementing a binary operator like `+` or `==`.
    // @param NAME  The C++ name of the Word object to define.
    // @param FORTHNAME  The word's Forth name (a string literal.)
    // @param INFIXOP  The raw C++ infix operator to implement, e.g. `+` or `==`.
    #define BINARY_OP_WORD(NAME, FORTHNAME, EFFECT, INFIXOP) \
        NATIVE_WORD(NAME, FORTHNAME, EFFECT) { \
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
        static constexpr Instruction const i_##NAME[] { __VA_ARGS__, _RETURN }; \
        constexpr Word NAME(FORTHNAME, EFFECT, i_##NAME)

}
