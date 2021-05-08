//
// word.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "instruction.hh"


/// Describes the effect upon the stack of a word.
/// `in` is how many values it reads from the stack; `out` is how many it leaves behind.
/// Thus the net effect on stack depth is `out - in`.
class StackEffect {
public:
    constexpr StackEffect()
    :_in(0), _net(0) { }

    constexpr StackEffect(uint8_t input, uint8_t output)
    :_in(input), _net(output - input) { }

    constexpr int input() const     {return _in;}
    constexpr int output() const    {return _in + _net;}
    constexpr int net() const       {return _net;}

    constexpr explicit operator bool() const {
        return _in > 0 || output() > 0;
    }

    constexpr bool operator== (const StackEffect &other) const {
        return _in == other._in && _net == other._net;
    }
    
    constexpr bool operator!= (const StackEffect &other) const {return !(*this == other);}

    /// Returns the cumulative effect of two StackEffects, first `this` and then `other`.
    constexpr StackEffect then(const StackEffect &other) const {
        int maxInput = std::max(this->input(), other.input() - this->net());
        return StackEffect(uint8_t(maxInput),
                           uint8_t(maxInput + this->net() + other.net()));
    }

    /// Returns the effect of doing either `this` or `other` (which must have the same net.)
    constexpr StackEffect merge(const StackEffect &other) const {
        assert(this->net() == other.net());
        return this->input() >= other.input() ? *this : other;
    }

private:
    uint8_t _in;
    int8_t _net;
};


/// A Forth word definition: name, flags and code.
/// This base class itself is used for predefined words.
struct Word {
    enum Flags : uint8_t {
        None = 0,
        Native = 1,
        HasIntParam = 2 ///< This word is followed by an int parameter (LITERAL, BRANCH, 0BRANCH)
    };

    constexpr Word(const char *name, Op native, StackEffect effect, Flags flags =None)
    :_instr(native)
    ,_name(name)
    ,_effect(effect)
    ,_flags(Flags(flags | Native))
    { }

    constexpr Word(const char *name, StackEffect effect, const Instruction words[])
    :_instr(words)
    ,_name(name)
    ,_effect(effect)
    ,_flags(None)
    { }

    constexpr operator Instruction() const        {return _instr;}

    constexpr bool isNative() const     {return (_flags & Native) != 0;}
    constexpr bool hasParam() const     {return (_flags & HasIntParam) != 0;}
    constexpr StackEffect stackEffect() const   {return _effect;}

    Instruction _instr; // Instruction that calls it (either an Op or an Instruction*)
    const char* _name;  // Forth name, or NULL if anonymous
    StackEffect _effect;
    Flags       _flags; // Flags (see above)

protected:
    Word() :_instr {}, _name(nullptr), _effect(0, 0), _flags(None) { };
};


// Shortcut for defining a native word (see examples in core_words.cc)
#define NATIVE_WORD(NAME, FORTHNAME, EFFECT, FLAGS) \
    extern "C" int* f_##NAME(int *sp, const Instruction *pc) noexcept; \
    constexpr Word NAME(FORTHNAME, f_##NAME, EFFECT, FLAGS); \
    extern "C" int* f_##NAME(int *sp, const Instruction *pc) noexcept


// Shortcut for defining a native word implementing a binary operator like `+` or `==`
#define BINARY_OP_WORD(NAME, FORTHNAME, INFIXOP) \
    NATIVE_WORD(NAME, FORTHNAME, StackEffect(2,1), Word::None) { \
        sp[1] = sp[1] INFIXOP sp[0];\
        ++sp;\
        NEXT(); \
    }


// Shortcut for defining an interpreted word (see examples in core_words.cc)
#define INTERP_WORD(NAME, FORTHNAME, EFFECT, ...) \
    static constexpr Instruction const i_##NAME[] { __VA_ARGS__, RETURN }; \
    constexpr Word NAME(FORTHNAME, EFFECT, i_##NAME)


