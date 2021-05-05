//
// tails.cc
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

/*
 TAILS is a very minimal, incomplete Forth interpreter core, made as a hack on May Fo[u]rth 2021
 by me, Jens Alfke (@snej on Github.)

 What can it do? Not much. It knows how to add and multiply integers!! and call functions!!!1!
 As proof, the test program in this source file evaluates ((4 + 3) ^2 * 2) ^2.
 It can't even load external programs; the program has to be hardcoded as a C++ static array.

 But it's miniscule! Only 172 bytes, and some of those are NOPs I couldn't get rid of.
 And it's very easy to extend -- see the to-do list at the end.

 The reason I wrote this, besides to fulfil a decades-old dream of building a working Forth
 interpreter, is to apply the really elegant and efficient implementation technique used by Wasm3
 (a WebAssembly interpreter), which is probably the fastest pure non-JIT interpreter there is.
 See below under "Performance".

 THREADING

 In Forth, "threading" refers to the unusual way the interpreter runs, not anything to do with
 concurrency. Forth code consists of a list of "word" (function) pointers, which the interpreter
 dispatches to one after the other. [1]

 Tails uses "direct threading", where each address in the code is literally a pointer to a native
 word. That makes dispatching as fast as possible. However, it wouldn't be a real language if
 you couldn't define functions in it, so there has to be a way to call a non-native word!
 This is done by the CALL word, which reads a Forth word address out of the instruction stream
 and calls it. The downsides are that calling Forth words is somewhat slower, and Forth words
 take up twice as much space in the code.

 (The opposite approach is called "indirect threading", where there's another layer of indirection
 between the pointers in a word and the native code to run. This adds overhead to each call,
 especially on modern CPUs where memory fetches are serious bottlenecks. But it's more flexible.)

 A great resource for learning how a traditional Forth interpreter works is JonesForth[2], a tiny
 interpreter in x86 assembly written in "literate" style, almost as much commentary as code.

 PERFORMANCE

 Tails was inspired by the design of the Wasm3 interpreter[3], which cleverly uses tail calls and
 register-based parameter passing to remove most of the overhead and allow C (or C++) code to
 be nearly as optimal as hand-written. I realized this would permit the bootstrap part of a
 Forth interpreter -- the part that implements the way words (functions) are called, and the core
 words -- to be written in C++.

 It did turn out to be as efficient as Steven Massey promised, though only with the proper
 build flags. For Clang, and I think also GCC, they are:
    -O3 -fomit-frame-pointer -fno-stack-check -fno-stack-protector
 The key parts are enabling optimizations so that tail calls become jumps, and disabling stack
 frames so functions don't unnecessarily push and pop the native stack.

 For example, here's the x86-64 assembly code of the PLUS function, compiled by Clang 11:
    3cd0    movl    (%rdi), %eax        ; load top of data stack into eax
    3cd2    addl    %eax, 0x4(%rdi)     ; add eax to second data stack item
    3cd5    addq    $0x4, %rdi          ; pop the data stack
    3cd9    movq    (%rsi), %rax        ; load next word pointer into rax
    3cdc    addq    $0x8, %rsi          ; bump the program counter
    3ce0    jmpq    *%rax               ; jump to the next word

 Storing the top-of-stack in a register/parameter would be even more optimal, I bet.

 Wasm3 doesn't use the native stack; instead it keeps its own call stack. This is probably more
 optimal, but I haven't done it yet simply because it's more work.

 TO DO LIST

 * Add an explicit interpreter call stack(?)
 * Add memory that programs can read/write
 * Add metadata for words: their name, attributes and code pointer.
 * Collect the metadata into a structure so a word can be looked up by name (regular Forths use a
   linked list.)
 * Define a bunch more core words in native code -- SWAP, ROT, `-`, ...
 * Define the basic lexer to read words from stdin
 * Define the all-important `:` and `;` words, so words can be defined in Forth itself...

 C VS C++

 I don't write in C. But this code uses very few C++ specific features, so with a few changes it
 could compile as C.

 * The constructors of Instruction -- you could remove them, but the function literals
   (`Square`, `Program`) would become uglier since you'd have to use C to initialize the unions.
 * The stack is declared as a `std::array`, but you could easily change that to a C array.

 [1]: http://www.complang.tuwien.ac.at/forth/threaded-code.html
 [2]: https://github.com/nornagon/jonesforth/blob/master/jonesforth.S
 [3]: https://github.com/wasm3/wasm3/blob/main/docs/Interpreter.md#m3-massey-meta-machine
 */


#include <array>
#include <initializer_list>
#include <stdlib.h>


// Magic function attributes!
//
//  Note: The `musttail` attribute is new (as of early 2021) and only supported by Clang.
//  Without it, ops will not use tail recursion _in unoptimized builds_, meaning
//  the call stack will grow with every word called and eventually overflow.
//  <https://clang.llvm.org/docs/AttributeReference.html#id398>
#if defined(__has_attribute) && __has_attribute(musttail)
#   define MUSTTAIL [[clang::musttail]]
#else
#   define MUSTTAIL
#endif

#define ALWAYS_INLINE [[gnu::always_inline]]
#define NOINLINE      [[gnu::noinline]]


// If ENABLE_TRACING is defined, a function `TRACE(sp,pc)` will be called after each Instruction.
#if DEBUG
#    define ENABLE_TRACING
#endif


//======================== INTERPRETER CORE ========================//


/// A native word is a C++ function with this signature.
/// Interpreted words consist of an array of (mostly) Op pointers,
/// but some native ops are followed by a parameter read by the function.
/// @param sp  Stack pointer. Top is sp[0], next is sp[1], ...
/// @param pc  Program counter. Points to the _next_ op to run.
/// @return    The stack pointer. (But almost all ops tail-call via `NEXT()`
///            instead of explicitly returning a value.)
using Op = int* (*)(int *sp, union Instruction *pc);


/// A Forth instruction. Code ("words") is a sequence of these.
union Instruction {
    Op           native;        // Every instruction starts with a native op
    int          param;         // Integer param after some ops like LITERAL, BRANCH, ...
    Instruction* word;          // This form appears after a CALL op

    Instruction(Op o)           :native(o) { }
    Instruction(int i)          :param(i) { }
    Instruction(Instruction *w) :word(w) { }

private:
    friend class Word;
    friend class WordRef;
    Instruction()               :word(nullptr) { }
};


/// Tracing function called at the end of each native op when `ENABLE_TRACING` is defined.
/// \warning Enabling this makes the code much less optimal, so only use when debugging.
#ifdef ENABLE_TRACING
    NOINLINE static void TRACE(int *sp, Instruction *pc);
#else
#   define TRACE(SP,PC)  (void)0
#endif


// The standard Forth NEXT routine, found at the end of every native op,
// that jumps to the next op.
// It uses tail-recursion, so (in an optimized build) it _literally does jump_,
// without growing the call stack.
#define NEXT()    TRACE(sp, pc); MUSTTAIL return pc->native(sp, pc + 1)


/// Calls an interpreted word pointed to by `fn`. Used by `CALL` and `run`.
/// @param sp    Stack pointer
/// @param instr The first instruction of the word to run
/// @return      The stack pointer on completion.
ALWAYS_INLINE
static inline int* call(int *sp, Instruction *instr) {
    return instr->native(sp, instr + 1);
    // TODO: It would be more efficient to avoid the native call stack,
    // and instead pass an interpreter call stack that `call` and `RETURN` manipulate.
}


//======================== DEFINING WORDS ========================//


struct Word;
extern Word CALL, RETURN, LITERAL;
struct WordRef;

/// Metadata of a word
struct Word {
    enum Flags {
        None = 0,
        HasIntParam = 1
    };

    Word(const char *name, Op native, Flags flags =None);

    Word(const char *name, std::initializer_list<WordRef> words);

    const Word*                    _prev;       // Previously-defined word
    const char*                    _name;       // Forth name
    Op                             _native {};  // Native function pointer or NULL
    std::unique_ptr<Instruction[]> _instrs {};  // Interpreted instructions or NULL
    Flags                          _flags {};

    static inline const Word* gLatest;          // Last-defined word; start of linked list
};


/// A reference to a Word, used in the initializer list of the Word constructor.
/// This is really just a convenience for hand-assembling words, not a real part of the system.
struct WordRef {
    WordRef(const Word &word) {
        assert(!(word._flags & Word::HasIntParam));
        if (word._native) {
            _instrs[0] = word._native;
            _count = 1;
        } else {
            assert(!word._native);
            _instrs[0] = CALL._native;
            _instrs[1] = word._instrs.get();
        }
    }

    WordRef(int i)
    :_instrs{LITERAL._native, i}
    { }

    WordRef(const Word &word, int param)
    :_instrs{word._native, param}
    {
        assert(word._native);
        assert(word._flags & Word::HasIntParam);
    }

    Instruction _instrs[2];
    int8_t      _count = 2;
};


/// Constructor for a native word.
Word::Word(const char *name, Op native, Flags flags)
:_prev(gLatest)
,_name(name)
,_native(native)
,_flags(flags)
{
    gLatest = this;
}


/// Constructor for an interpreted word.
Word::Word(const char *name, std::initializer_list<WordRef> words)
:_prev(gLatest)
,_name(name)
{
    size_t count = 1;
    for (auto &ref : words)
        count += ref._count;
    _instrs.reset(new Instruction[count]);
    Instruction *dst = &_instrs[0];
    for (auto &ref : words) {
        std::copy(&ref._instrs[0], &ref._instrs[ref._count], dst);
        dst += ref._count;
    }
    *dst = RETURN._native;

    gLatest = this;
}


#define NATIVE_WORD(NAME, FORTHNAME, FLAGS) \
    static int* f_##NAME(int *sp, Instruction *pc); \
    Word NAME(FORTHNAME, f_##NAME, FLAGS); \
    static int* f_##NAME(int *sp, Instruction *pc)

#define BINARY_OP_WORD(NAME, FORTHNAME, INFIXOP) \
    NATIVE_WORD(NAME, FORTHNAME, Word::None) { \
        sp[1] = sp[1] INFIXOP sp[0];\
        ++sp;\
        NEXT(); \
    }


//======================== NATIVE WORDS ========================//


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


//======================== TOP LEVEL ========================//


/// The data stack. Starts at `DataStack.back()` and grows downwards/backwards.
static std::array<int,1000> DataStack;


/// Initializes & runs the interpreter, and returns the top value left on the stack.
static int run(const Word &word) {
    assert(word._instrs);
    return * call(DataStack.end(), word._instrs.get());
}


//======================== TEST CODE ========================//


#ifdef ENABLE_TRACING
    /// Tracing function called at the end of each native op -- prints the stack
    static void TRACE(int *sp, Instruction *pc) {
        printf("At %p: ", pc);
        for (auto i = &DataStack.back(); i >= sp; --i)
            printf(" %d", *i);
        putchar('\n');
    }
#endif


int main(int argc, char *argv[]) {
    printf("Known words:");
    for (auto word = Word::gLatest; word; word = word->_prev)
        printf(" %s", word->_name);
    printf("\n");

    const Word Program("Program", {
        4,
        3,
        PLUS,
        SQUARE,
        DUP,
        PLUS,
        SQUARE,
        ABS
    });

    int n = run(Program);

    printf("Result is %d\n", n);
    if (n != 9604)
        abort();
    printf("\t\t❣️❣️❣️\n\n");
    return 0;
}
