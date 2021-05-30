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


namespace tails::core_words {

#pragma mark NATIVE WORDS:


    // NOTE: "Magic" words that don't appear in source code are prefixed with an underscore.


#pragma mark The absolute core:

    // Calls the interpreted word pointed to by the following instruction.
    NATIVE_WORD_PARAMS(_INTERP, "_INTERP", StackEffect::weird(), Word::Magic | Word::HasWordParam, 1) {
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    NATIVE_WORD_PARAMS(_INTERP2, "_INTERP2", StackEffect::weird(), Word::Magic | Word::HasWordParam, 2) {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    NATIVE_WORD_PARAMS(_INTERP3, "_INTERP3", StackEffect::weird(), Word::Magic | Word::HasWordParam, 3) {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    NATIVE_WORD_PARAMS(_INTERP4, "_INTERP4", StackEffect::weird(), Word::Magic | Word::HasWordParam, 4) {
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        sp = call(sp, (pc++)->word);
        NEXT();
    }

    // Jumps to the interpreted word pointed to by the following instruction.
    NATIVE_WORD(_TAILINTERP, "_TAILINTERP", StackEffect::weird(), Word::Magic) {
        MUSTTAIL return call(sp, pc->word);
    }

    // Returns from the current word. Every interpreted word ends with this.
    NATIVE_WORD(_RETURN, "_RETURN", StackEffect(), Word::Magic) {
        return sp;
    }

    //  Pushes the following instruction as a Value
    NATIVE_WORD(_LITERAL, "_LITERAL", "-- v", Word::Magic | Word::HasValParam) {
        *(++sp) = (pc++)->literal;
        NEXT();
    }

#pragma mark Stack gymnastics:

    NATIVE_WORD(DUP, "DUP", "a -- a a", 0) {
        ++sp;
        sp[0] = sp[-1];
        NEXT();
    }

    NATIVE_WORD(DROP, "DROP", "a --", 0) {
        --sp;
        NEXT();
    }

    NATIVE_WORD(SWAP, "SWAP", "a b -- b a", 0) {
        std::swap(sp[0], sp[-1]);
        NEXT();
    }

    NATIVE_WORD(OVER, "OVER", "a b -- a b a", 0) {
        ++sp;
        sp[0] = sp[-2];
        NEXT();
    }

    NATIVE_WORD(ROT, "ROT", "a b c -- b c a", 0) {
        auto sp2 = sp[-2];
        sp[-2] = sp[-1];
        sp[-1] = sp[0];
        sp[0] = sp2;
        NEXT();
    }

    // A placeholder used by the compiler that doesn't actually appear in code
    NATIVE_WORD(NOP, "NOP", "--", 0) {
        NEXT();
    }

#pragma mark Control Flow:

    /* "It turns out that all you need in order to define looping constructs, IF-statements, etc.
        are two primitives.
        BRANCH is an unconditional branch.
        0BRANCH is a conditional branch (it only branches if the top of stack is zero)." --JonesForth */

    // reads offset from *pc
    NATIVE_WORD(_BRANCH, "BRANCH", "--", Word::Magic | Word::HasIntParam) {
        pc += pc->offset + 1;
        NEXT();
    }

    // reads offset from *pc ... Assumes Value supports operator `!`
    NATIVE_WORD(_ZBRANCH, "0BRANCH", "b --", Word::Magic | Word::HasIntParam) {
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
    // Stack effect is dependent on quote1 and quote2
    NATIVE_WORD(IFELSE, "IFELSE", StackEffect::weird(), 0) {
        const Word *quote = (!!sp[-2] ? sp[-1] : sp[0]).asQuote();
        sp = call(sp - 3, quote->instruction().word);
        NEXT();
    }
#endif // SIMPLE_VALUE

#pragma mark Arithmetic & Relational:

    NATIVE_WORD(ZERO, "0", "-- #", 0) {
        *(++sp) = Value(0);
        NEXT();
    }

    NATIVE_WORD(ONE, "1", "-- #", 0) {
        *(++sp) = Value(1);
        NEXT();
    }

    BINARY_OP_WORD(PLUS,  "+",   "a#$ b#$ -- a#$", +)
    BINARY_OP_WORD(MINUS, "-",   "# # -- #", -)
    BINARY_OP_WORD(MULT,  "*",   "# # -- #", *)
    BINARY_OP_WORD(DIV,   "/",   "# # -- #", /)
    BINARY_OP_WORD(MOD,   "MOD", "# # -- #", %)
    BINARY_OP_WORD(EQ,    "=",   "# # -- #", ==)
    BINARY_OP_WORD(NE,    "<>",  "x y -- #", !=)
    BINARY_OP_WORD(GT,    ">",   "x y -- #", >)
    BINARY_OP_WORD(GE,    ">=",  "x y -- #", >=)
    BINARY_OP_WORD(LT,    "<",   "x y -- #", <)
    BINARY_OP_WORD(LE,    "<=",  "x y -- #", <=)

    NATIVE_WORD(EQ_ZERO, "0=",  "a -- #", 0)  { sp[0] = Value(sp[0] == Value(0)); NEXT(); }
    NATIVE_WORD(NE_ZERO, "0<>", "a -- #", 0)  { sp[0] = Value(sp[0] != Value(0)); NEXT(); }
    NATIVE_WORD(GT_ZERO, "0>",  "a -- #", 0)  { sp[0] = Value(sp[0] > Value(0)); NEXT(); }
    NATIVE_WORD(LT_ZERO, "0<",  "a -- #", 0)  { sp[0] = Value(sp[0] < Value(0)); NEXT(); }

#ifndef SIMPLE_VALUE

    // [Appended an "_" to the symbol name to avoid conflict with C's `NULL`.]
    NATIVE_WORD(NULL_, "NULL", "-- ?", 0) {
        *(++sp) = NullValue;
        NEXT();
    }

#pragma mark Strings & Arrays:

    NATIVE_WORD(LENGTH, "LENGTH", "${} -- #", 0) {
        *sp = sp->length();
        NEXT();
    }


#endif

#pragma mark - INTERPRETED WORDS:


    // Warning: A numeric literal has to be preceded by _LITERAL, an interpreted word by _INTERP.


    INTERP_WORD(ABS, "ABS", StackEffect("a# -- abs#").withMax(1),
        DUP,
        LT_ZERO,
        _ZBRANCH, Instruction::withOffset(3),
        ZERO,
        SWAP,
        MINUS
    );

    INTERP_WORD(MAX, "MAX", StackEffect("a# b# -- max#").withMax(2),
        OVER,
        OVER,
        LT,
        _ZBRANCH, Instruction::withOffset(1),
        SWAP,
        DROP
    );


    INTERP_WORD (MIN, "MIN", StackEffect("a# b# -- min#").withMax(2),
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
        &_TAILINTERP, &_LITERAL, &_RETURN, &_BRANCH, &_ZBRANCH,
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
