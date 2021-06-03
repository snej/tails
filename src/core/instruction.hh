//
// instruction.hh
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
#include "platform.hh"
#include "value.hh"


namespace tails {
    union Instruction;


    // If ENABLE_TRACING is defined, a function `TRACE(sp,pc)` will be called after each Instruction.
    // Enabling this makes the code much less optimal, so only use when debugging.
    #if DEBUG
    #    define ENABLE_TRACING
    #endif

    #ifdef ENABLE_TRACING
        NOINLINE void TRACE(Value *sp, const Instruction *pc);
    #else
    #   define TRACE(SP,PC)  (void)0
    #endif


    /// A native word is a C++ function with this signature.
    /// Interpreted words consist of an array of (mostly) Op pointers,
    /// but some native ops are followed by a parameter read by the function.
    /// @param sp  Stack pointer. Top is sp[0], below is sp[-1], sp[-2] ...
    /// @param pc  Program counter. Points to the _next_ op to run.
    /// @return    The updated stack pointer. (But almost all ops tail-call via `NEXT()`
    ///            instead of explicitly returning a value.)
    using Op = Value* (*)(Value *sp, const Instruction *pc);


    /// A Forth instruction. Interpreted code is a sequence of these.
    union Instruction {
        Op                 native;  // Every instruction starts with a native op
        const Instruction* word;    // Interpreted word to call; parameter to INTERP
        intptr_t           offset;  // PC offset; parameter to BRANCH and ZBRANCH
        Value              literal; // Value to push on stack; parameter to LITERAL

        constexpr Instruction(Op o)                 :native(o) { }
        constexpr Instruction(const Instruction *w) :word(w) { }
        constexpr Instruction(Value v)              :literal(v) { }
        explicit constexpr Instruction(intptr_t o)  :offset(o) { }

        static constexpr Instruction withOffset(intptr_t o) {return Instruction(o);}

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


    /// Calls an interpreted word pointed to by `fn`. Used by `INTERP` and `run`.
    /// @param sp    Stack pointer
    /// @param start The first instruction of the word to run
    /// @return      The stack pointer on completion.
    ALWAYS_INLINE
    static inline Value* call(Value *sp, const Instruction *start) {
        return start->native(sp, start + 1);
    }

}
