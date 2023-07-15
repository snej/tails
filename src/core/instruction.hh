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
#include "utils.hh"
#include "value.hh"


namespace tails {
    struct Instruction;


    /// Definition of the Tails bytecodes, i.e. native words.
    /// If this is changed you MUST also change the arrays `Opcodes` and `OpWords` in core_words.cc!
    enum class Opcode : uint8_t {
        _INTERP, _TAILINTERP,
        _LITERAL, _INT, _RETURN, _BRANCH, _ZBRANCH,
        NOP, _RECURSE,
        DROP, DUP, OVER, ROT, _ROTn, SWAP,
        ZERO, ONE,
        EQ, NE, EQ_ZERO, NE_ZERO,
        GE, GT, GT_ZERO,
        LE, LT, LT_ZERO,
        ABS, MAX, MIN,
        DIV, MOD, MINUS, MULT, PLUS,
        CALL,
        NULL_,
        LENGTH,
        IFELSE,
        DEFINE,
        _GETARG, _SETARG, _LOCALS, _DROPARGS,
        PRINT, SP, NL, NLQ,

        none = 255
    };


    // If ENABLE_TRACING is defined, a function `TRACE(sp,pc)` will be called before each Instruction.
    // Enabling this makes the code much less optimal, so only use when debugging.
    #if DEBUG
    #    define ENABLE_TRACING
    #endif

    #ifdef ENABLE_TRACING
        NOINLINE void TRACE(Value *sp, const Instruction *pc);
    #else
    #   define TRACE(SP,PC)  (void)0
    #endif


    // What comes after opcode of an Instruction: either a param or the next Instruction's opcode.
    union AfterInstruction {
        struct DropCount {          // Used by _DROPARGS instruction
            uint8_t locals, results;
        };

        const Instruction* word;    // - Interpreted code to call; parameter to INTERP
        int16_t            offset;  // - PC offset; parameter to BRANCH and ZBRANCH
        DropCount          drop;    // - Number of locals to drop & results to keep, for _DROPARGS
        Value              literal; // - Value to push on stack; parameter to LITERAL
        Opcode             nextOp;  // - If there are no parameters, the next Instruction is here

        constexpr AfterInstruction() :word(nullptr) { }

        Instruction const* next() const  {return (Instruction*)&nextOp;}
        AfterInstruction const* afterNext() const {return (AfterInstruction const*)(&nextOp + 1);}
    } __attribute__((aligned (1))) __attribute__((packed));


    /// A Forth instruction. Interpreted code is a sequence of these.
    struct Instruction {
        Opcode              opcode = Opcode::none;  // Every instruction starts with an opcode
        AfterInstruction    param;

        explicit constexpr Instruction(Opcode o)             :opcode(o) { }
        explicit constexpr Instruction(const Instruction *w) {param.word = w;}
        explicit constexpr Instruction(Value v)              {param.literal = v;}
        explicit constexpr Instruction(AfterInstruction::DropCount d) {param.drop = d;}
        explicit constexpr Instruction(int16_t o)   {param.offset = o;}

        static constexpr Instruction withOffset(int16_t o) {return Instruction(o);}

        /// The native Word that implements my opcode.
        inline Word const* nativeWord() const;

        /// A pointer to the next Instruction, skipping any parameters.
        inline Instruction const* next() const;

        /// Copies an Instruction, being careful not to read beyond the bounds of its parameters.
        /// This avoids undefined behavior or crashes when copying from real code.
        inline Instruction carefulCopy() const;

    private:
        friend class Word;
        friend class WordRef;
        constexpr Instruction()                     :Instruction(nullptr) { }
    };

    inline bool operator== (const Instruction &a, const Instruction &b) {return a.opcode == b.opcode;}
    inline bool operator!= (const Instruction &a, const Instruction &b) {return !(a == b);}

    // The standard Forth NEXT routine, found at the end of every native op,
    // that jumps to the next op.
    // It uses tail-recursion, so (in an optimized build) it _literally does jump_,
    // without growing the call stack.
    Value* _next(Value *sp, const AfterInstruction* pc);
    #define NEXT() MUSTTAIL return _next(sp, pc)


    /// Calls an interpreted word pointed to by `fn`. Used by `run`.
    /// @param sp    Stack pointer
    /// @param start The first instruction of the word to run
    /// @return      The stack pointer on completion.
    [[nodiscard]]
    ALWAYS_INLINE
    static inline Value* call(Value *sp, const Instruction *start) {
        return _next(sp, (AfterInstruction*)start);
    }


}
