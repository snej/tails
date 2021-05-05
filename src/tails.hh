//
// tails.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "platform.hh"
#include <initializer_list>
#include <memory>
#include <stdlib.h>


union Instruction;


//======================== TRACING ========================//


// If ENABLE_TRACING is defined, a function `TRACE(sp,pc)` will be called after each Instruction.
// Enabling this makes the code much less optimal, so only use when debugging.
#if DEBUG
#    define ENABLE_TRACING
#endif

#ifdef ENABLE_TRACING
    NOINLINE void TRACE(int *sp, const Instruction *pc);
#else
#   define TRACE(SP,PC)  (void)0
#endif


//======================== INTERPRETER CORE ========================//


/// A native word is a C++ function with this signature.
/// Interpreted words consist of an array of (mostly) Op pointers,
/// but some native ops are followed by a parameter read by the function.
/// @param sp  Stack pointer. Top is sp[0], next is sp[1], ...
/// @param pc  Program counter. Points to the _next_ op to run.
/// @return    The stack pointer. (But almost all ops tail-call via `NEXT()`
///            instead of explicitly returning a value.)
using Op = int* (*)(int *sp, const Instruction *pc);


/// A Forth instruction. Interpreted code is a sequence of these.
union Instruction {
    Op                 native; // Every instruction starts with a native op
    int                param;  // Integer param after some ops like LITERAL, BRANCH, ...
    const Instruction* word;   // This form appears after a CALL op

    constexpr Instruction(Op o)                 :native(o) { }
    constexpr Instruction(int i)                :param(i) { }
    constexpr Instruction(const Instruction *w) :word(w) { }

private:
    friend class Word;
    friend class WordRef;
    constexpr Instruction()                     :word(nullptr) { }
};


// The standard Forth NEXT routine, found at the end of every native op,
// that jumps to the next op.
// It uses tail-recursion, so (in an optimized build) it _literally does jump_,
// without growing the call stack.
#define NEXT()    TRACE(sp, pc); MUSTTAIL return pc->native(sp, pc + 1)


/// Calls an interpreted word pointed to by `fn`. Used by `CALL` and `run`.
/// @param sp    Stack pointer
/// @param start The first instruction of the word to run
/// @return      The stack pointer on completion.
ALWAYS_INLINE
static inline int* call(int *sp, const Instruction *start) {
    return start->native(sp, start + 1);
}


//======================== DEFINING WORDS ========================//


struct WordRef;


/// A Forth word definition: name, flags and code.
struct Word {
    enum Flags {
        None = 0,
        HasIntParam = 1 ///< This word is followed by an int parameter (LITERAL, BRANCH, 0BRANCH)
    };

    Word(const char *name, Op native, Flags flags =None);
    Word(const char *name, std::initializer_list<WordRef> words);
    Word(std::initializer_list<WordRef> words)  :Word(nullptr, words) { }

    const char*                    _name;       // Forth name
    Op                             _native {};  // Native function pointer or NULL
    std::unique_ptr<Instruction[]> _instrs {};  // Interpreted instructions or NULL
    Flags                          _flags {};
};


/// A reference to a Word and optional following parameter;
/// used only temporarily, in the initializer_list of the Word constructor.
/// This is just a convenience for hand-assembling words, not a real part of the system.
struct WordRef {
    WordRef(const Word &word);
    WordRef(const Word &word, int param);
    WordRef(int i);

    Instruction _instrs[2];
    int8_t      _count = 2;
};


