//
// native_word.hh
//
// 
//

#pragma once
#include "instruction.hh"
#include "word.hh"

namespace tails {

    // Declarations used when implementing native (bytecode) words.

    /// A native word, implementing an Opcode, is a C++ function with this signature.
    /// Interpreted words consist of an array of (mostly) Op pointers,
    /// but some native ops are followed by a parameter read by the function.
    /// @param sp  Stack pointer. Top is sp[0], below is sp[-1], sp[-2] ...
    /// @param pc  Program counter. Points to the _next_ op to run.
    /// @return    The updated stack pointer. (But almost all ops tail-call via `NEXT()`
    ///            instead of explicitly returning a value.)
    using Op = Value* (*)(Value *sp, const AfterInstruction* pc);

    /// Reads an instruction parameter from the code. For use in native words.
    template <typename PARAM>
    inline PARAM readParam(const AfterInstruction* &pc) {
        PARAM p;
        ::memcpy(&p, pc, sizeof(PARAM));
        pc = offsetby(pc, sizeof(PARAM));
        return p;
    }

    /// Syntactic sugar: `PARAM(pc->offset)` instead of `readParam<int16_t>(pc)`
    #define PARAM(P)    readParam<std::remove_const<__typeof(P)>::type>(pc)


    // Shortcut for defining a native word (see examples in core_words.cc.)
    // It should be followed by the C++ function body in curly braces.
    // The body can use parameters `sp` and `pc`, and should end by calling `NEXT()`.
    // @param NAME  The C++ name of the Word object to define.
    // @param FORTHNAME  The word's Forth name (a string literal.)
    // @param EFFECT  The \ref StackEffect. Must be accurate!
    // Flags and parameter count may optionally follow, as per the Word constructor.
    #define NATIVE_WORD(NAME, FORTHNAME, EFFECT, ...) \
        extern "C" Value* f_##NAME(Value *sp, const AfterInstruction* pc); \
        constexpr Word NAME(FORTHNAME, Opcode::NAME, EFFECT, ## __VA_ARGS__); \
        Value* f_##NAME(Value *sp, const AfterInstruction* pc) // { ...function body follows... }


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

}
