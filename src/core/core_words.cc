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
#include "stack_effect.hh"


namespace tails::core_words {

    static constexpr TypeSet
        Any = TypeSet::anyType(),
        Nul = TypeSet(Value::ANull),
        Num = TypeSet(Value::ANumber),
        Str = TypeSet(Value::AString),
        Arr = TypeSet(Value::AnArray);


#pragma mark NATIVE WORDS:


#pragma mark The absolute core:

    // Calls the interpreted word pointed to by the following instruction.
    NATIVE_WORD(_INTERP, "_INTERP", StackEffect::weird(),
                Word::MagicWordParam)
    {
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // Returns from the current word. Every interpreted word ends with this.
    NATIVE_WORD(_RETURN, "_RETURN", StackEffect(),
                Word::Magic)
    {
        return sp;
    }

    // Pushes the following instruction as a Value.
    // (The stack effect is declared as untyped, but the stack checker sees the literal value on
    // the simulated stack and knows its exact type.)
    NATIVE_WORD(_LITERAL, "_LITERAL", StackEffect({}, {Any}),
                Word::MagicValParam)
    {
        *(++sp) = (pc++)->literal;
        NEXT();
    }


#pragma mark - CALL OPTIMIZATIONS:

    // Interprets 2 following words (saving an instruction and some clock cycles.)
    NATIVE_WORD(_INTERP2, "_INTERP2", StackEffect::weird(),
                Word::MagicWordParam, 2)
    {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // Interprets 3 following words.
    NATIVE_WORD(_INTERP3, "_INTERP3", StackEffect::weird(),
                Word::MagicWordParam, 3)
    {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // Interprets 4 following words.
    NATIVE_WORD(_INTERP4, "_INTERP4", StackEffect::weird(),
                Word::MagicWordParam, 4)
    {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // _Jumps_ to the interpreted word pointed to by the following instruction,
    // as a tail-call optimization. The stack does not grow.
    // This must of course be the last word before a _RETURN.
    // (The _RETURN could then be optional, except it's currently used when inlining.)
    NATIVE_WORD(_TAILINTERP, "_TAILINTERP", StackEffect::weird(),
                Word::MagicWordParam, 1)
    {
        MUSTTAIL return call(sp, pc->word);
    }

    // Interprets 2 following words, jumping to the last one.
    NATIVE_WORD(_TAILINTERP2, "_TAILINTERP2", StackEffect::weird(),
                Word::MagicWordParam, 2)
    {
        sp = call(sp, (pc++)->word);
        MUSTTAIL return call(sp, pc->word);
    }

    // Interprets 3 following words, jumping to the last one.
    NATIVE_WORD(_TAILINTERP3, "_TAILINTERP3", StackEffect::weird(),
                Word::MagicWordParam, 3)
    {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        MUSTTAIL return call(sp, pc->word);
    }

    // Interprets 4 following words, jumping to the last one.
    NATIVE_WORD(_TAILINTERP4, "_TAILINTERP4", StackEffect::weird(),
                Word::MagicWordParam, 4)
    {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        MUSTTAIL return call(sp, pc->word);
    }

    // There's no reason there couldn't be more of these: _INTERP5, _INTERP6, ...
    // They'd need to be implemented here, and added to kWords and kInterpWords.


#pragma mark Stack gymnastics:

    NATIVE_WORD(DUP, "DUP", StackEffect({Any}, {Any/0, Any/0})) {
        ++sp;
        sp[0] = sp[-1];
        NEXT();
    }

    NATIVE_WORD(DROP, "DROP", StackEffect({Any}, {})) {
        --sp;
        NEXT();
    }

    NATIVE_WORD(SWAP, "SWAP", StackEffect({Any,   Any},
                                          {Any/0, Any/1}))
    {
        std::swap(sp[0], sp[-1]);
        NEXT();
    }

    NATIVE_WORD(OVER, "OVER", StackEffect({Any,   Any},
                                          {Any/1, Any/0, Any/1}))
    {
        ++sp;
        sp[0] = sp[-2];
        NEXT();
    }

    NATIVE_WORD(ROT, "ROT", StackEffect({Any,   Any,   Any},
                                        {Any/1, Any/0, Any/2}))
    {
        auto sp2 = sp[-2];
        sp[-2] = sp[-1];
        sp[-1] = sp[ 0];
        sp[ 0] = sp2;
        NEXT();
    }

    // A placeholder used by the compiler that doesn't actually appear in code
    NATIVE_WORD(NOP, "NOP", StackEffect()) {
        NEXT();
    }


#pragma mark Control Flow:

    /* "It turns out that all you need in order to define looping constructs, IF-statements, etc.
        are two primitives.
        BRANCH is an unconditional branch.
        0BRANCH is a conditional branch (it only branches if the top of stack is zero)." --JonesForth
        But t
     */

    // reads offset from *pc
    NATIVE_WORD(_BRANCH, "BRANCH", StackEffect(),
                Word::MagicIntParam)
    {
        pc += pc->offset + 1;
        NEXT();
    }

    // reads offset from *pc ... Assumes Value supports `operator !`
    NATIVE_WORD(_ZBRANCH, "0BRANCH", StackEffect({Any}, {}),
                Word::MagicIntParam)
    {
        if (!(*sp--))
            pc += pc->offset;
        ++pc;
        NEXT();
    }

#ifndef SIMPLE_VALUE
    // (? quote -> ?)  Pops a quotation (word) and calls it.
    // The actual stack effect is that of the quotation it calls, which in the general case is
    // only known at runtime. Until the compiler's stack checker can deal with this, I'm making
    // this word Magic so it can't be used in source code.
    NATIVE_WORD(CALL, "CALL", StackEffect::weird(),
                Word::Magic)
    {
        const Word *quote = (*sp--).asQuote();
        assert(quote);                                  // FIXME: Handle somehow; exceptions?
        sp = call(sp, quote->instruction().word);
        NEXT();
    }


#pragma mark Higher Order Functions (Combinators):

    // (b quote1 quote2 -> ?)  Pops params, then evals quote1 if b is truthy, else quote2.
    // Stack effect is dependent on quote1 and quote2; currently this word is special-cased by
    // the compiler's stack-checker.
    NATIVE_WORD(IFELSE, "IFELSE", StackEffect::weird()) {
        const Word *quote = (!!sp[-2] ? sp[-1] : sp[0]).asQuote();
        sp = call(sp - 3, quote->instruction().word);
        NEXT();
    }
#endif // SIMPLE_VALUE



#pragma mark Arithmetic & Relational:

    // These assume the C++ Value type supports arithmetic and relational operators.

    NATIVE_WORD(ZERO, "0", StackEffect({}, {Num})) {
        *(++sp) = Value(0);
        NEXT();
    }

    NATIVE_WORD(ONE, "1", StackEffect({}, {Num})) {
        *(++sp) = Value(1);
        NEXT();
    }

    static constexpr StackEffect kBinEffect({Num, Num}, {Num});
    static constexpr StackEffect kRelEffect({Any, Any}, {Num});
    static constexpr StackEffect k0RelEffect({Any}, {Num});

#ifdef SIMPLE_VALUE
    BINARY_OP_WORD(PLUS,  "+",   kBinEffect, +)
#else
    BINARY_OP_WORD(PLUS,  "+",   StackEffect({Num|Str|Arr, Num|Str|Arr},
                                             {(Num|Str|Arr)/0}),
                   +)
#endif
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

#ifndef SIMPLE_VALUE

    // [Appended an "_" to the symbol name to avoid conflict with C's `NULL`.]
    NATIVE_WORD(NULL_, "NULL", StackEffect({}, {Nul})) {
        *(++sp) = NullValue;
        NEXT();
    }


#pragma mark Strings & Arrays:

    NATIVE_WORD(LENGTH, "LENGTH", StackEffect({Str|Arr}, {Num})) {
        *sp = sp->length();
        NEXT();
    }


#endif


#pragma mark - INTERPRETED WORDS:

    // These could easily be implemented in native code, but I'm making them interpreted for now
    // so the interpreted call path gets more use. --jpa May 2021

    // Warning: A numeric literal has to be preceded by _LITERAL, an interpreted word by _INTERP.

    INTERP_WORD(ABS, "ABS", StackEffect({Num}, {Num}).withMax(1),
        DUP,
        LT_ZERO,
        _ZBRANCH, Instruction::withOffset(3),
        ZERO,
        SWAP,
        MINUS
    );

    INTERP_WORD(MAX, "MAX", StackEffect({Any, Any}, {Any}).withMax(2),
        OVER,
        OVER,
        LT,
        _ZBRANCH, Instruction::withOffset(1),
        SWAP,
        DROP
    );


    INTERP_WORD (MIN, "MIN", StackEffect({Any, Any}, {Any}).withMax(2),
        OVER,
        OVER,
        GT,
        _ZBRANCH, Instruction::withOffset(1),
        SWAP,
        DROP
    );


#pragma mark - LIST OF CORE WORDS:


    // This null-terminated list is used to register these words in the Vocabulary at startup.

    const Word* const kWords[] = {
        &_INTERP, &_INTERP2, &_INTERP3, &_INTERP4,
        &_TAILINTERP, &_TAILINTERP2, &_TAILINTERP3, &_TAILINTERP4, 
        &_LITERAL, &_RETURN, &_BRANCH, &_ZBRANCH,
        &DROP, &DUP, &OVER, &ROT, &SWAP, &NOP,
        &ZERO, &ONE,
        &EQ, &NE, &EQ_ZERO, &NE_ZERO,
        &GE, &GT, &GT_ZERO,
        &LE, &LT, &LT_ZERO,
        &ABS, &MAX, &MIN,
        &DIV, &MOD, &MINUS, &MULT, &PLUS,
#ifndef SIMPLE_VALUE
        &CALL,
        &NULL_,
        &LENGTH,
        &IFELSE,
        &DEFINE,
#endif
        nullptr
    };

}
