//
// word.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "instruction.hh"


/// A Forth word definition: name, flags and code.
/// This base class itself is used for predefined words.
struct Word {
    enum Flags {
        None = 0,
        Native = 1,
        HasIntParam = 2 ///< This word is followed by an int parameter (LITERAL, BRANCH, 0BRANCH)
    };

    constexpr Word(const char *name, Op native, Flags flags =None)
    :_instr(native)
    ,_name(name)
    ,_flags(Flags(flags | Native))
    { }

    constexpr Word(const char *name, const Instruction words[])
    :_instr(words)
    ,_name(name)
    ,_flags(None)
    { }

    constexpr operator Instruction() const        {return _instr;}

    constexpr bool isNative() const     {return (_flags & Native) != 0;}
    constexpr bool hasParam() const     {return (_flags & HasIntParam) != 0;}

    Instruction _instr; // Instruction that calls it (either an Op or an Instruction*)
    const char* _name;  // Forth name, or NULL if anonymous
    Flags       _flags; // Flags (see above)

protected:
    Word() :_instr {}, _name(nullptr), _flags(None) { };
};


// Shortcut for defining a native word (see examples in core_words.cc)
#define NATIVE_WORD(NAME, FORTHNAME, FLAGS) \
    extern "C" int* f_##NAME(int *sp, const Instruction *pc) noexcept; \
    constexpr Word NAME(FORTHNAME, f_##NAME, FLAGS); \
    extern "C" int* f_##NAME(int *sp, const Instruction *pc) noexcept


// Shortcut for defining a native word implementing a binary operator like `+` or `==`
#define BINARY_OP_WORD(NAME, FORTHNAME, INFIXOP) \
    NATIVE_WORD(NAME, FORTHNAME, Word::None) { \
        sp[1] = sp[1] INFIXOP sp[0];\
        ++sp;\
        NEXT(); \
    }


// Shortcut for defining an interpreted word (see examples in core_words.cc)
#define INTERP_WORD(NAME, FORTHNAME, ...) \
    static constexpr Instruction const i_##NAME[] { __VA_ARGS__, RETURN }; \
    constexpr Word NAME(FORTHNAME, i_##NAME)


