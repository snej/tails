//
// instruction.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "platform.hh"
#include <initializer_list>
#include <stdlib.h>
#include <vector>


namespace tails {
    union Instruction;


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

    inline bool operator== (const Instruction &a, const Instruction &b) {return a.native == b.native;}
    inline bool operator!= (const Instruction &a, const Instruction &b) {return !(a == b);}


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

}
