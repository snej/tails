//
// tails.cc
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

#include <array>
#include <cstdlib>
#include <iostream>


// If defined, a function TRACE(sp,pc) will be called after each instruction.
#define ENABLE_TRACING


union Instruction;

/// A native word is a function with this signature.
/// Interpreted words consist of an array of (mostly) Op pointers.
/// @param sp  Stack pointer. Top is sp[0], next is sp[1], ...
/// @param pc  Program counter. Points to the _next_ op to run.
/// @return    The ending stack pointer. (But almost all ops tail-call
///            instead of explicitly returning a value.)
using Op = int* (*)(int *sp, Instruction *pc);


/// A Forth instruction. Code is a sequence of these.
union Instruction {
    Op           native;        // Every instruction starts with a native op
    int          literal;       // This form appears after a LITERAL op
    Instruction* word;          // This form appears after a CALL op

    Instruction(Op o)           :native(o) { }
    Instruction(int i)          :literal(i) { }
    Instruction(Instruction *w) :word(w) { }
};


/// Tracing function called at the end of each native op when `ENABLE_TRACING` is defined.
#ifdef ENABLE_TRACING
    static void TRACE(int *sp, Instruction *pc);
#else
#   define TRACE(SP,PC)  0
#endif


/// The standard Forth NEXT routine, found at the end of every native op,
/// that jumps to the next op.
/// It uses tail-recursion, so (in an optimized build) it literally does jump,
/// without growing the call stack.
#define NEXT()    return TRACE(sp, pc), pc->native(sp, pc + 1)


/// Calls an interpreted word pointed to by `fn`. Used by `CALL` and `run`.
#define _CALL(fn)  auto _dst = (Instruction*)(fn); sp = _dst->native(sp, _dst + 1)


/***************** NATIVE OPS *****************/


// ( -> i)  Pushes the following instruction as an integer
static int* LITERAL(int *sp, Instruction *pc) {
    *(--sp) = (pc++)->literal;
    NEXT();
}

// (a -> a a)
static int* DUP(int *sp, Instruction *pc) {
    --sp;
    sp[0] = sp[1];
    NEXT();
}

// (a b -> a+b)
static int* PLUS(int *sp, Instruction *pc) {
    sp[1] += sp[0];
    ++sp;
    NEXT();
}

// (a b -> a*b)
static int* MULT(int *sp, Instruction *pc) {
    sp[1] *= sp[0];
    ++sp;
    NEXT();
}

// ( -> )  Returns from the current function.
static int* RETURN(int *sp, Instruction *pc) {
    return sp;
}


// ( ? -> ? )  Calls the subroutine pointed to by the following instruction
static int* CALL(int *sp, Instruction *pc) {
    _CALL((pc++)->word);
    NEXT();
}


/***************** TOP LEVEL *****************/


/// The data stack. Starts at `DataStack.back()` and grows downwards.
static std::array<int,1000> DataStack;


/// Runs the interpreter, starting at the given pc,
/// and returns the top value on the stack.
static int run(Instruction* start) {
    int *sp = DataStack.end();
    _CALL(start);
    return *sp;
}


/***************** TEST CODE *****************/


using namespace std;


static Instruction Square[] = {
    DUP,
    MULT,
    RETURN
};


static Instruction Program[] = {
    LITERAL, 4,
    LITERAL, 3,
    PLUS,
    CALL,    Square,
    DUP,
    PLUS,
    CALL,    Square,
    RETURN
};


/// Tracing function called at the end of each native op -- prints the stack
#ifdef ENABLE_TRACING
    static void TRACE(int *sp, Instruction *pc) {
        cout << "At " << (void*)pc << ": ";
        for (auto i = &DataStack.back(); i >= sp; --i)
            cout << ' ' << *i;
        cout << '\n';
    }
#endif


int main(int argc, char *argv[]) {
    cout << "Program is at " << (void*)Program << "\n";
    cout << "Square  is at " << (void*)Square  << "\n";

    int n = run(Program);

    cout << "Result is " << n;
    if (n != 9604)
        abort();
    cout << "  ❣️❣️❣️\n\n";
    return 0;
}
