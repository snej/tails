//
// core_words.cc
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

// Reference: <https://forth-standard.org/standard/core>

#include "core_words.hh"
#include "native_word.hh"
#include "stack_effect.hh"
#include <cmath>


namespace tails::core_words {
    
    static constexpr ROMTypeSet
        Any = ROMTypeSet::anyType(),
        Nul = ROMTypeSet(Value::ANull),
        Num = ROMTypeSet(Value::ANumber),
        Str = ROMTypeSet(Value::AString),
        Arr = ROMTypeSet(Value::AnArray);


    [[nodiscard]]
    ALWAYS_INLINE
    static inline Value* call(Value *sp, const AfterInstruction *pc) {
        return _next(sp, pc);
    }


#pragma mark NATIVE WORDS:


#pragma mark The absolute core:
    
    // A placeholder used by the compiler that doesn't actually appear in code
    NATIVE_WORD(NOP, "NOP", ROMStackEffect()) {
        NEXT();
    }
    
    // Calls the interpreted word pointed to by the following instruction.
    NATIVE_WORD(_INTERP, "_INTERP", ROMStackEffect::weird(),
                Word::MagicWordParam)
    {
        sp = call(sp, PARAM(pc->word));
        NEXT();
    }
    
    // Returns from the current word. Every interpreted word ends with this.
    NATIVE_WORD(_RETURN, "_RETURN", ROMStackEffect(),
                Word::Magic)
    {
        return sp;
    }
    
    // Pushes the following instruction as a Value.
    // (The stack effect is declared as untyped, but the stack checker sees the literal value on
    // the simulated stack and knows its exact type.)
    NATIVE_WORD(_LITERAL, "_LITERAL", ROMStackEffect({}, {Any}),
                Word::MagicValParam)
    {
        *(++sp) = PARAM(pc->literal);
        NEXT();
    }
    
    
    // Pushes the following small integer as a Value.
    NATIVE_WORD(_INT, "_INT", ROMStackEffect({}, {Num}),
                Word::MagicIntParam)
    {
        *(++sp) = PARAM(pc->offset);
        NEXT();
    }
    
    
    // _Jumps_ to the interpreted word pointed to by the following instruction,
    // as a tail-call optimization. The stack does not grow.
    // This must of course be the last word before a _RETURN.
    // (The _RETURN could then be optional, except it's currently used when inlining.)
    NATIVE_WORD(_TAILINTERP, "_TAILINTERP", ROMStackEffect::weird(),
                Word::MagicWordParam, 1)
    {
        MUSTTAIL return call(sp, &PARAM(pc->word)->param);
    }


#pragma mark Stack gymnastics:
    
    NATIVE_WORD(DUP, "DUP", ROMStackEffect({Any}, {Any/0, Any/0})) {
        ++sp;
        sp[0] = sp[-1];
        NEXT();
    }
    
    NATIVE_WORD(DROP, "DROP", ROMStackEffect({Any}, {})) {
        --sp;
        NEXT();
    }
    
    NATIVE_WORD(SWAP, "SWAP", ROMStackEffect({Any,   Any},
                                          {Any/0, Any/1}))
    {
        std::swap(sp[0], sp[-1]);
        NEXT();
    }
    
    NATIVE_WORD(OVER, "OVER", ROMStackEffect({Any,   Any},
                                          {Any/1, Any/0, Any/1}))
    {
        ++sp;
        sp[0] = sp[-2];
        NEXT();
    }
    
    NATIVE_WORD(ROT, "ROT", ROMStackEffect({Any,   Any,   Any},
                                        {Any/1, Any/0, Any/2}))
    {
        auto sp2 = sp[-2];
        sp[-2] = sp[-1];
        sp[-1] = sp[ 0];
        sp[ 0] = sp2;
        NEXT();
    }

    // Arbitrary stack rotation. Positive n moves item at depth n to the top of the stack;
    // negative n moves the top of the stack to depth n.
    // * ROTn<1> and ROTn<-1> are both equivalent to SWAP.
    // * ROTn<2> is equivalent to ROT.
    NATIVE_WORD(_ROTn, "ROTn", ROMStackEffect::weird(),
                Word::MagicIntParam, 1)
    {
        auto n = PARAM(pc->offset);
        if (n > 0) {
            auto newTop = sp[-n];
            ::memmove(&sp[-n], &sp[-n+1], n * sizeof(Value));
            sp[0] = newTop;
        } else {
            assert(n != 0);
            auto oldTop = sp[0];
            ::memmove(&sp[-n+1], &sp[-n], n * sizeof(Value));
            sp[-n] = oldTop;
       }
        NEXT();
    }
    
#pragma mark Named Function Arguments & Locals:
    
    
    // Pushes null values on the stack to serve as local variables. Param is number to push.
    NATIVE_WORD(_LOCALS, "_LOCALS", ROMStackEffect::weird(),
                Word::MagicIntParam, 1)
    {
        for (auto n = PARAM(pc->offset); n > 0; --n)
            *++sp = Value();
        NEXT();
    }
    
    // Pushes an arg/local on the stack. Param is its current stack offset (negative!)
    // _GETARG<0> is DUP, _GETARG<-1> is OVER, ...
    NATIVE_WORD(_GETARG, "_GETARG", ROMStackEffect::weird(),  // or, StackEffect({}, {Any})
                Word::MagicIntParam, 1)
    {
        auto n = PARAM(pc->offset);
        assert(n <= 0);
        auto val = sp[n];
        *++sp = val;
        NEXT();
    }
    
    // Writes to a function parameter. Pops a value and stores it `n` items back in the stack.
    NATIVE_WORD(_SETARG, "_SETARG", ROMStackEffect::weird(),  // or, StackEffect({Any}, {})
                Word::MagicIntParam, 1)
    {
        auto n = PARAM(pc->offset);
        assert(n <= 0);
        sp[n] = *sp;
        --sp;
        NEXT();
    }
    
    // Removes unconsumed function args and local variables from the stack at the end of a function.
    // The lower 16 bits of the param is the number of function args & locals to remove;
    // The upper bits of the param is the number of function results on top to preserve.
    NATIVE_WORD(_DROPARGS, "_DROPARGS", ROMStackEffect::weird(),
                Word::MagicIntParam, 1)
    {
        auto drop = PARAM(pc->drop);
        if (drop.results > 0)
            memmove(&sp[-int(drop.locals)],
                    &sp[1-int(drop.results)],
                    drop.results * sizeof(*sp));
        sp -= drop.locals;
        NEXT();
    }
    
    
#pragma mark Control Flow:
    
    /* "It turns out that all you need in order to define looping constructs, IF-statements, etc.
     are two primitives.
     BRANCH is an unconditional branch.
     0BRANCH is a conditional branch (it only branches if the top of stack is zero)." --JonesForth
     */
    
    // reads offset from *pc
    NATIVE_WORD(_BRANCH, "BRANCH", ROMStackEffect(),
                Word::MagicIntParam)
    {
        pc = offsetby(pc, pc->offset);
        NEXT();
    }
    
    // reads offset from *pc ... Assumes Value supports `operator !`
    NATIVE_WORD(_ZBRANCH, "0BRANCH", ROMStackEffect({Any}, {}),
                Word::MagicIntParam)
    {
        if (!(*sp--))
            pc = offsetby(pc, pc->offset);
        else
            pc = offsetby(pc, sizeof(pc->offset));
        NEXT();
    }
    
    // recursively calls the current word. The offset back to the start of the word is stored at
    // *pc, so this is similar to a BRANCH back to the start, except it uses `call`.
    NATIVE_WORD(_RECURSE, "_RECURSE", ROMStackEffect::weird(),
                Word::MagicIntParam)
    {
        sp = call(sp, offsetby(pc, pc->offset));
        pc = offsetby(pc, sizeof(pc->offset));
        NEXT();
    }
    
    
    // (? quote -> ?)  Pops a quotation (word) and calls it.
    // The actual stack effect is that of the quotation it calls, which in the general case is
    // only known at runtime. Until the compiler's stack checker can deal with this, I'm making
    // this word Magic so it can't be used in source code.
    NATIVE_WORD(CALL, "CALL", ROMStackEffect::weird(),
                Word::Magic)
    {
        const Word *quote = (*sp--).asQuote();
        assert(quote);                                  // FIXME: Handle somehow; exceptions?
        sp = call(sp, quote->instruction().param.word);
        NEXT();
    }
    
    
#pragma mark Higher Order Functions (Combinators):
    
    // (b quote1 quote2 -> ?)  Pops params, then evals quote1 if b is truthy, else quote2.
    // Stack effect is dependent on quote1 and quote2; currently this word is special-cased by
    // the compiler's stack-checker.
    NATIVE_WORD(IFELSE, "IFELSE", ROMStackEffect::weird()) {
        const Word *quote = (!!sp[-2] ? sp[-1] : sp[0]).asQuote();
        sp = call(sp - 3, quote->instruction().param.word);
        NEXT();
    }
    
    
#pragma mark Arithmetic & Relational:
    
    // These assume the C++ Value type supports arithmetic and relational operators.
    
    NATIVE_WORD(ZERO, "0", ROMStackEffect({}, {Num})) {
        *(++sp) = Value(0);
        NEXT();
    }
    
    NATIVE_WORD(ONE, "1", ROMStackEffect({}, {Num})) {
        *(++sp) = Value(1);
        NEXT();
    }
    
    static constexpr ROMStackEffect kBinEffect({Num, Num}, {Num});
    static constexpr ROMStackEffect kRelEffect({Any, Any}, {Num});
    static constexpr ROMStackEffect k0RelEffect({Any}, {Num});
    
    BINARY_OP_WORD(PLUS,  "+",   ROMStackEffect({Num|Str|Arr, Num|Str|Arr},
                                             {(Num|Str|Arr)/0}),
                   +)
    BINARY_OP_WORD(MINUS, "-",   kBinEffect, -)
    BINARY_OP_WORD(MULT,  "*",   kBinEffect, *)
    BINARY_OP_WORD(DIV,   "/",   kBinEffect, /)
    BINARY_OP_WORD(MOD,   "MOD", kBinEffect, %)
    
    BINARY_OP_WORD(EQ,    "=",   kRelEffect, ==)
    BINARY_OP_WORD(NE,    "<>",  kRelEffect, !=)
    BINARY_OP_WORD(GT,    ">",   kRelEffect, >)
    BINARY_OP_WORD(GE,    ">=",  kRelEffect, >=)
    BINARY_OP_WORD(LT,    "<",   kRelEffect, <)
    BINARY_OP_WORD(LE,    "<=",  kRelEffect, <=)
    
    NATIVE_WORD(EQ_ZERO, "0=",  k0RelEffect)  { sp[0] = Value(sp[0] == Value(0)); NEXT(); }
    NATIVE_WORD(NE_ZERO, "0<>", k0RelEffect)  { sp[0] = Value(sp[0] != Value(0)); NEXT(); }
    NATIVE_WORD(GT_ZERO, "0>",  k0RelEffect)  { sp[0] = Value(sp[0] >  Value(0)); NEXT(); }
    NATIVE_WORD(LT_ZERO, "0<",  k0RelEffect)  { sp[0] = Value(sp[0] <  Value(0)); NEXT(); }

    NATIVE_WORD(ABS, "ABS", ROMStackEffect({Num}, {Num})) {
        *sp = Value(::abs(sp->asDouble()));
        NEXT();
    }

    NATIVE_WORD(MIN, "MIN", ROMStackEffect({Any, Any}, {Any/1})) {
        if (sp[0] < sp[-1])
            sp[-1] = sp[0];
        --sp;
        NEXT();
    }

    NATIVE_WORD(MAX, "MAX", ROMStackEffect({Any, Any}, {Any/1})) {
        if (sp[0] > sp[-1])
            sp[-1] = sp[0];
        --sp;
        NEXT();
    }

    // [Appended an "_" to the symbol name to avoid conflict with C's `NULL`.]
    NATIVE_WORD(NULL_, "NULL", ROMStackEffect({}, {Nul})) {
        *(++sp) = NullValue;
        NEXT();
    }
    
    
#pragma mark Strings & Arrays:
    
    NATIVE_WORD(LENGTH, "LENGTH", ROMStackEffect({Str|Arr}, {Num})) {
        *sp = sp->length();
        NEXT();
    }
    
}


#include "more_words.hh"

namespace tails {
    using namespace core_words;


    extern "C" {
        Value* f_DEFINE(Value *sp, const AfterInstruction* pc);  // compiler.cc
        Value* f_PRINT(Value *sp, const AfterInstruction* pc);  // more_words.cc
        Value* f_SP(Value *sp, const AfterInstruction* pc);  // more_words.cc
        Value* f_NL(Value *sp, const AfterInstruction* pc);  // more_words.cc
        Value* f_NLQ(Value *sp, const AfterInstruction* pc);  // more_words.cc
    }

    // The native function pointer for each opcode.
    static const Op Opcodes[256] = {
#define DEFINE_OP(O) &f_##O,
#include "opcodes.hh"
    };

    // The Word describing each opcode.
    const Word* const OpWords[256] = {
#define DEFINE_OP(O) &O,
#include "opcodes.hh"
    };


    Value* _next(Value *sp, const AfterInstruction* pc) {
        TRACE(sp, pc->next());
        MUSTTAIL return Opcodes[uint8_t(pc->nextOp)](sp, pc->afterNext());
    }

}
