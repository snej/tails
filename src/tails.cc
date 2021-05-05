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
using Op = int* (*)(int *sp, const union Instruction *pc);


/// A Forth instruction. Code ("words") is a sequence of these.
union Instruction {
    Op           native;        // Every instruction starts with a native op
    int          param;         // Integer param after some ops like LITERAL, BRANCH, ...
    const Instruction* word;          // This form appears after a CALL op

    constexpr Instruction(Op o)           :native(o) { }
    constexpr Instruction(int i)          :param(i) { }
    constexpr Instruction(const Instruction *w) :word(w) { }

private:
    friend class Word;
    friend class WordRef;
    constexpr Instruction()               :word(nullptr) { }
};


/// Tracing function called at the end of each native op when `ENABLE_TRACING` is defined.
/// \warning Enabling this makes the code much less optimal, so only use when debugging.
#ifdef ENABLE_TRACING
    NOINLINE static void TRACE(int *sp, const Instruction *pc);
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
static inline int* call(int *sp, const Instruction *instr) {
    return instr->native(sp, instr + 1);
    // TODO: It would be more efficient to avoid the native call stack,
    // and instead pass an interpreter call stack that `call` and `RETURN` manipulate.
}


//======================== DEFINING WORDS ========================//


struct Word;
struct WordRef;
extern const Word CALL, RETURN, LITERAL;


/// A Forth word definition: name, flags and code.
struct Word {
    enum Flags {
        None = 0,
        HasIntParam = 1
    };

    Word(const char *name, Op native, Flags flags =None);

    Word(const char *name, std::initializer_list<WordRef> words);

    Word(std::initializer_list<WordRef> words)  :Word(nullptr, words) { }

    mutable const Word*            _prev;       // Previously-defined word
    const char*                    _name;       // Forth name
    Op                             _native {};  // Native function pointer or NULL
    std::unique_ptr<Instruction[]> _instrs {};  // Interpreted instructions or NULL
    Flags                          _flags {};

    static inline const Word* gLatest;          // Last-defined word; start of linked list
};


/// A reference to a Word and optional following parameter;
/// used only temporarily, in the initializer list of the Word constructor.
/// This is just a convenience for hand-assembling words, not a real part of the system.
struct WordRef {
    constexpr WordRef(const Word &word) {
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

    constexpr WordRef(const Word &word, int param)
    :_instrs{word._native, param}
    {
        assert(word._native);
        assert(word._flags & Word::HasIntParam);
    }

    constexpr WordRef(int i)
    :WordRef{LITERAL, i}
    { }

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
:_prev(name ? gLatest : nullptr)
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

    if (name)
        gLatest = this;
}


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


//======================== TOP LEVEL ========================//


/// The data stack. Starts at `DataStack.back()` and grows downwards/backwards.
static std::array<int,1000> DataStack;


/// Top-level function to run a Word.
/// @return  The top value left on the stack.
static int run(const Word &word) {
    assert(word._instrs); // must be interpreted
    return * call(DataStack.end(), word._instrs.get());
}


/// Top-level function to run an anonymous temporary Word.
/// @return  The top value left on the stack.
static int run(std::initializer_list<WordRef> words) {
    return run(Word(words));
}


//======================== TEST CODE ========================//


#ifdef ENABLE_TRACING
    /// Tracing function called at the end of each native op -- prints the stack
    static void TRACE(int *sp, const Instruction *pc) {
        printf("\tat %p: ", pc);
        for (auto i = &DataStack.back(); i >= sp; --i)
            printf(" %d", *i);
        putchar('\n');
    }
#endif


static void _test(std::initializer_list<WordRef> words, const char *sourcecode, int expected) {
    printf("* Testing {%s} ...\n", sourcecode);
    int n = run(words);
    printf("\t-> got %d\n", n);
    assert(n == expected);
}

#define TEST(EXPECTED, ...) _test({__VA_ARGS__}, #__VA_ARGS__, EXPECTED)


int main(int argc, char *argv[]) {
    printf("Known words:");
    for (auto word = Word::gLatest; word; word = word->_prev)
        printf(" %s", word->_name);
    printf("\n");

    TEST(-1234, -1234);
    TEST(-1,    3, 4, MINUS);
    TEST(1,     1, 2, 3, ROT);
    TEST(16,    4, SQUARE);
    TEST(1234,  -1234, ABS);
    TEST(1234,  1234, ABS);
    TEST(4,     3, 4, MAX);
    TEST(4,     4, 3, MAX);

    TEST(9604,
         4,
         3,
         PLUS,
         SQUARE,
         DUP,
         PLUS,
         SQUARE,
         ABS);

    printf("\nTESTS PASSED❣️❣️❣️\n\n");
    return 0;
}
