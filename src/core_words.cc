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


//---- The absolute core:

// (? -> ?)  Calls the subroutine pointed to by the following instruction.
NATIVE_WORD(CALL, "CALL", StackEffect(1,1), {}) {
    sp = call(sp, (pc++)->word);
    NEXT();
}

// ( -> )  Returns from the current word. Every word ends with this.
NATIVE_WORD(RETURN, "RETURN", StackEffect(0,0), {}) {
    return sp;
}

// ( -> i)  Pushes the following instruction as an integer
NATIVE_WORD(LITERAL, "LITERAL", StackEffect(0,1), Word::HasIntParam) {
    *(--sp) = (pc++)->param;
    NEXT();
}

//---- Stack gymnastics:

// (a -> a a)
NATIVE_WORD(DUP, "DUP", StackEffect(1,2), {}) {
    --sp;
    sp[0] = sp[1];
    NEXT();
}

// (a -> )
NATIVE_WORD(DROP, "DROP", StackEffect(1,0), {}) {
    ++sp;
    NEXT();
}

// (a b -> b a)
NATIVE_WORD(SWAP, "SWAP", StackEffect(2,2), {}) {
    std::swap(sp[0], sp[1]);
    NEXT();
}

// (a b -> a b a)
NATIVE_WORD(OVER, "OVER", StackEffect(2,3), {}) {
    --sp;
    sp[0] = sp[2];
    NEXT();
}

// (a b c -> b c a)
NATIVE_WORD(ROT, "ROT", StackEffect(3,3), {}) {
    auto sp2 = sp[2];
    sp[2] = sp[1];
    sp[1] = sp[0];
    sp[0] = sp2;
    NEXT();
}

// ( -> )
NATIVE_WORD(NOP, "NOP", StackEffect(0,0), {}) {
    NEXT();
}

//---- Arithmetic & Relational:

// ( -> 0)
NATIVE_WORD(ZERO, "0", StackEffect(0,1), {}) {
    *(--sp) = 0;
    NEXT();
}

// ( -> 1)
NATIVE_WORD(ONE, "1", StackEffect(0,1), {}) {
    *(--sp) = 1;
    NEXT();
}

// (a b -> a{op}b)
BINARY_OP_WORD(PLUS,  "+",  +)
BINARY_OP_WORD(MINUS, "-",  -)
BINARY_OP_WORD(MULT,  "*",  *)
BINARY_OP_WORD(DIV,   "/",  /)
BINARY_OP_WORD(MOD,   "MOD", %)
BINARY_OP_WORD(EQ,    "=",  ==)
BINARY_OP_WORD(NE,    "<>", !=)
BINARY_OP_WORD(GT,    ">",  >)
BINARY_OP_WORD(GE,    ">=", >=)
BINARY_OP_WORD(LT,    "<",  <)
BINARY_OP_WORD(LE,    "<=", <=)

// (a -> bool)
NATIVE_WORD(EQ_ZERO, "0=", StackEffect(1,1), {})  { sp[0] = (sp[0] == 0); NEXT(); }
NATIVE_WORD(NE_ZERO, "0<>", StackEffect(1,1), {}) { sp[0] = (sp[0] != 0); NEXT(); }
NATIVE_WORD(GT_ZERO, "0>", StackEffect(1,1), {})  { sp[0] = (sp[0] > 0); NEXT(); }
NATIVE_WORD(LT_ZERO, "0<", StackEffect(1,1), {})  { sp[0] = (sp[0] < 0); NEXT(); }

//---- Control Flow:

/* "It turns out that all you need in order to define looping constructs, IF-statements, etc.
    are two primitives.
    BRANCH is an unconditional branch.
    0BRANCH is a conditional branch (it only branches if the top of stack is zero)." --JonesForth */

// ( -> )  and reads offset from *pc
NATIVE_WORD(BRANCH, "BRANCH", StackEffect(0,0), Word::HasIntParam) {
    pc += pc->param + 1;
    NEXT();
}

// (b -> )  and reads offset from *pc
NATIVE_WORD(ZBRANCH, "0BRANCH", StackEffect(1,0), Word::HasIntParam) {
    if (*sp++ == 0)
        pc += pc->param;
    ++pc;
    NEXT();
}


//======================== INTERPRETED WORDS ========================//


// Warning: A numeric literal has to be preceded by LITERAL, and an interpreted word by CALL.


// (a -> a^2)
INTERP_WORD(SQUARE, "SQUARE", StackEffect(1,1, 1),
    DUP,
    MULT
);


// (a -> abs)
INTERP_WORD(ABS, "ABS", StackEffect(1,1, 1),
    DUP,
    LT_ZERO,
    ZBRANCH, 3,
    ZERO,
    SWAP,
    MINUS
);

// (a b -> max)
INTERP_WORD(MAX, "MAX", StackEffect(2,1, 2),
    OVER,
    OVER,
    LT,
    ZBRANCH, 1,
    SWAP,
    DROP
);


// (a b -> min)
INTERP_WORD (MIN, "MIN", StackEffect(2,1, 2),
    OVER,
    OVER,
    GT,
    ZBRANCH, 1,
    SWAP,
    DROP
);


const Word* const kCoreWords[] = {
    &CALL, &LITERAL, &RETURN,
    &DROP, &DUP, &OVER, &ROT, &SWAP, &NOP,
    &EQ, &NE, &EQ_ZERO, &NE_ZERO,
    &GE, &GT, &GT_ZERO,
    &LE, &LT, &LT_ZERO,
    &ABS, &MAX, &MIN, &SQUARE,
    &DIV, &MOD, &MINUS, &MULT, &PLUS,
    &BRANCH, &ZBRANCH,
    nullptr
};

