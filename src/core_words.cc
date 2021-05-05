//
// core_words.cc
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
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

#include "core_words.hh"


//======================== NATIVE WORDS ========================//


// Shortcut for defining a native word
#define NATIVE_WORD(NAME, FORTHNAME, FLAGS) \
    static int* f_##NAME(int *sp, const Instruction *pc); \
    const Word NAME(FORTHNAME, f_##NAME, FLAGS); \
    static int* f_##NAME(int *sp, const Instruction *pc)

// Shortcut for defining a native word implementing a binary operator like `+` or `==`
#define BINARY_OP_WORD(NAME, FORTHNAME, INFIXOP) \
    NATIVE_WORD(NAME, FORTHNAME, Word::None) { \
        sp[1] = sp[1] INFIXOP sp[0];\
        ++sp;\
        NEXT(); \
    }


//---- The absolute core:

// (? -> ?)  Calls the subroutine pointed to by the following instruction.
NATIVE_WORD(CALL, "CALL", {}) {
    sp = call(sp, (pc++)->word);
    NEXT();
}

// ( -> )  Returns from the current word. Every word ends with this.
NATIVE_WORD(RETURN, "RETURN", {}) {
    return sp;
}

// ( -> i)  Pushes the following instruction as an integer
NATIVE_WORD(LITERAL, "LITERAL", Word::HasIntParam) {
    *(--sp) = (pc++)->param;
    NEXT();
}

//---- Stack gymnastics:

// (a -> a a)
NATIVE_WORD(DUP, "DUP", {}) {
    --sp;
    sp[0] = sp[1];
    NEXT();
}

// (a -> )
NATIVE_WORD(DROP, "DROP", {}) {
    ++sp;
    NEXT();
}

// (a b -> b a)
NATIVE_WORD(SWAP, "SWAP", {}) {
    std::swap(sp[0], sp[1]);
    NEXT();
}

// (a b -> a b a)
NATIVE_WORD(OVER, "OVER", {}) {
    --sp;
    sp[0] = sp[2];
    NEXT();
}

// (a b c -> b c a)
NATIVE_WORD(ROT, "ROT", {}) {
    auto sp2 = sp[2];
    sp[2] = sp[1];
    sp[1] = sp[0];
    sp[0] = sp2;
    NEXT();
}

//---- Arithmetic & Relational:

NATIVE_WORD(ZERO, "0", {}) {
    *(--sp) = 0;
    NEXT();
}

NATIVE_WORD(ONE, "1", {}) {
    *(--sp) = 1;
    NEXT();
}

BINARY_OP_WORD(PLUS,  "+",  +)
BINARY_OP_WORD(MINUS, "-",  -)
BINARY_OP_WORD(MULT,  "*",  *)
BINARY_OP_WORD(DIV,   "/",  /)
BINARY_OP_WORD(MOD,   "MOD", %)
BINARY_OP_WORD(EQ,    "=",  ==)
BINARY_OP_WORD(NE,    "<>", !=)
BINARY_OP_WORD(GT,    ">",  >)
BINARY_OP_WORD(GE,    "<=", <=)
BINARY_OP_WORD(LT,    "<",  <)
BINARY_OP_WORD(LE,    "<=", <=)

NATIVE_WORD(EQ_ZERO, "0=", {})  { sp[0] = (sp[0] == 0); NEXT(); }
NATIVE_WORD(NE_ZERO, "0<>", {}) { sp[0] = (sp[0] != 0); NEXT(); }
NATIVE_WORD(GT_ZERO, "0>", {})  { sp[0] = (sp[0] > 0); NEXT(); }
NATIVE_WORD(LT_ZERO, "0<", {})  { sp[0] = (sp[0] < 0); NEXT(); }

//---- Control Flow:

/* "It turns out that all you need in order to define looping constructs, IF-statements, etc.
    are two primitives.
    BRANCH is an unconditional branch.
    0BRANCH is a conditional branch (it only branches if the top of stack is zero)." --JonesForth */

NATIVE_WORD(BRANCH, "BRANCH", Word::HasIntParam) {
    pc += (pc++)->param;
    NEXT();
}

NATIVE_WORD(ZBRANCH, "0BRANCH", Word::HasIntParam) {
    if (*sp++ == 0)
        pc += pc->param;
    ++pc;
    NEXT();
}


//======================== INTERPRETED WORDS ========================//


const Word SQUARE("SQUARE", {
    DUP,
    MULT,
});


const Word ABS("ABS", {
    DUP,
    LT_ZERO,
    {ZBRANCH, 3},
    ZERO,
    SWAP,
    MINUS
});


const Word MAX("MAX", {
    OVER,
    OVER,
    LT,
    {ZBRANCH, 1},
    SWAP,
    DROP
});


const Word MIN("MIN", {
    OVER,
    OVER,
    GT,
    {ZBRANCH, 1},
    SWAP,
    DROP
});
