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
#include "stack_effect_parser.hh"


namespace tails::core_words {


#pragma mark NATIVE WORDS:


    // NOTE: "Magic" words that don't appear in source code are prefixed with an underscore.



#pragma mark The absolute core:

    // Calls the interpreted word pointed to by the following instruction.
    NATIVE_WORD_PARAMS(_INTERP, "_INTERP", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 1)
    {
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // Returns from the current word. Every interpreted word ends with this.
    NATIVE_WORD(_RETURN, "_RETURN", StackEffect(), Word::Magic) {
        return sp;
    }

    // Pushes the following instruction as a Value.
    // (The stack effect is declared as untyped, but the stack checker sees the literal value on
    // the simulated stack and knows its exact type.)
    NATIVE_WORD_PARAMS(_LITERAL, "_LITERAL", "-- v"_sfx, Word::Magic | Word::HasValParam, 1) {
        *(++sp) = (pc++)->literal;
        NEXT();
    }


#pragma mark - CALL OPTIMIZATIONS:

    // Interprets 2 following words (saving an instruction and some clock cycles.)
    NATIVE_WORD_PARAMS(_INTERP2, "_INTERP2", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 2)
    {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // Interprets 3 following words.
    NATIVE_WORD_PARAMS(_INTERP3, "_INTERP3", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 3)
    {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // Interprets 4 following words.
    NATIVE_WORD_PARAMS(_INTERP4, "_INTERP4", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 4)
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
    NATIVE_WORD_PARAMS(_TAILINTERP, "_TAILINTERP", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 1) {
        MUSTTAIL return call(sp, pc->word);
    }

    // Interprets 2 following words, jumping to the last one.
    NATIVE_WORD_PARAMS(_TAILINTERP2, "_TAILINTERP2", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 2) {
        sp = call(sp, (pc++)->word);
        MUSTTAIL return call(sp, pc->word);
    }

    // Interprets 3 following words, jumping to the last one.
    NATIVE_WORD_PARAMS(_TAILINTERP3, "_TAILINTERP3", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 3) {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        MUSTTAIL return call(sp, pc->word);
    }

    // Interprets 4 following words, jumping to the last one.
    NATIVE_WORD_PARAMS(_TAILINTERP4, "_TAILINTERP4", StackEffect::weird(),
                       Word::Magic | Word::HasWordParam, 4) {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        MUSTTAIL return call(sp, pc->word);
    }

    // There's no reason there couldn't be more of these: _INTERP5, _INTERP6, ...
    // They'd need to be implemented here, and added to kWords and kInterpWords.


#pragma mark Stack gymnastics:

    NATIVE_WORD(DUP, "DUP", "a -- a a"_sfx, 0) {
        ++sp;
        sp[0] = sp[-1];
        NEXT();
    }

    NATIVE_WORD(DROP, "DROP", "a --"_sfx, 0) {
        --sp;
        NEXT();
    }

    NATIVE_WORD(SWAP, "SWAP", "a b -- b a"_sfx, 0) {
        std::swap(sp[0], sp[-1]);
        NEXT();
    }

    NATIVE_WORD(OVER, "OVER", "a b -- a b a"_sfx, 0) {
        ++sp;
        sp[0] = sp[-2];
        NEXT();
    }

    NATIVE_WORD(ROT, "ROT", "a b c -- b c a"_sfx, 0) {
        auto sp2 = sp[-2];
        sp[-2] = sp[-1];
        sp[-1] = sp[0];
        sp[0] = sp2;
        NEXT();
    }

    // A placeholder used by the compiler that doesn't actually appear in code
    NATIVE_WORD(NOP, "NOP", "--"_sfx, 0) {
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
    NATIVE_WORD_PARAMS(_BRANCH, "BRANCH", "--"_sfx, Word::Magic | Word::HasIntParam, 1) {
        pc += pc->offset + 1;
        NEXT();
    }

    // reads offset from *pc ... Assumes Value supports `operator !`
    NATIVE_WORD_PARAMS(_ZBRANCH, "0BRANCH", "b --"_sfx, Word::Magic | Word::HasIntParam, 1) {
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
    NATIVE_WORD(CALL, "CALL", StackEffect::weird(), Word::Magic) {
        const Word *quote = (*sp--).asQuote();
        assert(quote);                                  // FIXME: Handle somehow; exceptions?
        sp = call(sp, quote->instruction().word);
        NEXT();
    }


#pragma mark Higher Order Functions (Combinators):

    // (b quote1 quote2 -> ?)  Pops params, then evals quote1 if b is truthy, else quote2.
    // Stack effect is dependent on quote1 and quote2; currently this word is special-cased by
    // the compiler's stack-checker.
    NATIVE_WORD(IFELSE, "IFELSE", StackEffect::weird(), 0) {
        const Word *quote = (!!sp[-2] ? sp[-1] : sp[0]).asQuote();
        sp = call(sp - 3, quote->instruction().word);
        NEXT();
    }
#endif // SIMPLE_VALUE



#pragma mark Arithmetic & Relational:

    // These assume the C++ Value type supports arithmetic and relational operators.

    NATIVE_WORD(ZERO, "0", "-- #"_sfx, 0) {
        *(++sp) = Value(0);
        NEXT();
    }

    NATIVE_WORD(ONE, "1", "-- #"_sfx, 0) {
        *(++sp) = Value(1);
        NEXT();
    }

#ifdef SIMPLE_VALUE
    BINARY_OP_WORD(PLUS,  "+",   "a# b# -- a#"_sfx, +)
#else
    BINARY_OP_WORD(PLUS,  "+",   "a#$ b#$ -- a#$"_sfx, +)
#endif
    BINARY_OP_WORD(MINUS, "-",   "# # -- #"_sfx, -)
    BINARY_OP_WORD(MULT,  "*",   "# # -- #"_sfx, *)
    BINARY_OP_WORD(DIV,   "/",   "# # -- #"_sfx, /)
    BINARY_OP_WORD(MOD,   "MOD", "# # -- #"_sfx, %)
    BINARY_OP_WORD(EQ,    "=",   "# # -- #"_sfx, ==)
    BINARY_OP_WORD(NE,    "<>",  "x y -- #"_sfx, !=)
    BINARY_OP_WORD(GT,    ">",   "x y -- #"_sfx, >)
    BINARY_OP_WORD(GE,    ">=",  "x y -- #"_sfx, >=)
    BINARY_OP_WORD(LT,    "<",   "x y -- #"_sfx, <)
    BINARY_OP_WORD(LE,    "<=",  "x y -- #"_sfx, <=)

    NATIVE_WORD(EQ_ZERO, "0=",  "a -- #"_sfx, 0)  { sp[0] = Value(sp[0] == Value(0)); NEXT(); }
    NATIVE_WORD(NE_ZERO, "0<>", "a -- #"_sfx, 0)  { sp[0] = Value(sp[0] != Value(0)); NEXT(); }
    NATIVE_WORD(GT_ZERO, "0>",  "a -- #"_sfx, 0)  { sp[0] = Value(sp[0] > Value(0)); NEXT(); }
    NATIVE_WORD(LT_ZERO, "0<",  "a -- #"_sfx, 0)  { sp[0] = Value(sp[0] < Value(0)); NEXT(); }

#ifndef SIMPLE_VALUE

    // [Appended an "_" to the symbol name to avoid conflict with C's `NULL`.]
    NATIVE_WORD(NULL_, "NULL", "-- ?"_sfx, 0) {
        *(++sp) = NullValue;
        NEXT();
    }


#pragma mark Strings & Arrays:

    NATIVE_WORD(LENGTH, "LENGTH", "$[] -- #"_sfx, 0) {
        *sp = sp->length();
        NEXT();
    }


#endif


#pragma mark - INTERPRETED WORDS:

    // These could easily be implemented in native code, but I'm making them interpreted for now
    // so the interpreted call path gets more use. --jpa May 2021

    // Warning: A numeric literal has to be preceded by _LITERAL, an interpreted word by _INTERP.

    INTERP_WORD(ABS, "ABS", StackEffect("a# -- abs#"_sfx).withMax(1),
        DUP,
        LT_ZERO,
        _ZBRANCH, Instruction::withOffset(3),
        ZERO,
        SWAP,
        MINUS
    );

    INTERP_WORD(MAX, "MAX", StackEffect("a# b# -- max#"_sfx).withMax(2),
        OVER,
        OVER,
        LT,
        _ZBRANCH, Instruction::withOffset(1),
        SWAP,
        DROP
    );


    INTERP_WORD (MIN, "MIN", StackEffect("a# b# -- min#"_sfx).withMax(2),
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
#endif
        nullptr
    };

}
