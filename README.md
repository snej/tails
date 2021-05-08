# Tails, A Fast C+\+ Forth Core

**Tails** is a minimal, fast [Forth][FORTH]-like interpreter core written entirely in C+\+. I created it as a one-day hack to celebrate May Forth 2021 … then kept going because it's fun.

What can it do? Not much. It knows how to add and multiply integers!! and call functions!!!! It can evaluate `4 3, + SQUARE DUP + SQUARE ABS` and return the expected answer `9604`. That expression can be written as a hardcoded list of word references, or parsed from a string.

It's not much, but it's pretty tiny! The magic core code (`NEXT`, `CALL`, `RETURN`, `LITERAL`, `DUP`, etc.) is about 200SLOC and compiles to a few hundred bytes, many of which are NOPs the compiler adds for padding. The parser and compiler add a few KB more.
 
And it's very easy to extend -- see the to-do list at the end.

The reason I wrote this, besides to fulfil a decades-old dream of building a working Forth interpreter, is to apply the really elegant and efficient implementation technique used by [Wasm3][WASM3] (a [WebAssembly][WASM] interpreter), which is probably the fastest pure non-JIT interpreter there is. See below under "Performance".
 
## Just Show Me The Code!

* The absolute core is [instruction.hh](https://github.com/snej/tails/blob/main/src/instruction.hh), which defines how code is structured and called, and how one primitive/native word proceeds to the next.
* [word.hh](https://github.com/snej/tails/blob/main/src/word.hh) defines the `Word` class that associates a name and flags with an (interpreted or primitive) function.
* [core_words.cc](https://github.com/snej/tails/blob/main/src/core_words.cc) defines the very basic primitives like `LITERAL`, `DUP`, `+`, and some interpreted words like `ABS` and `MAX`.
 
## Theory Of Operation

Tails's world is currently very simple: a stack of integers, a call stack, and a program consisting of an array of function pointers. That's not very useful in the real world, but it's "the simplest thing that could possibly work."

### Core (AKA native, primitive) functions

Tails's core primitives are C+\+ functions of the form `int* FN(int *sp, const Instruction *pc)`.

* `sp` is the stack pointer. It grows downward, so `sp[0]` is the top value, `sp[1]` is below it, etc.
* `pc` is the program counter, which points to an array of `Instruction`s, specifically to the next instruction to execute.
* The return value is the updated stack pointer.

The function body can read and write values on the stack relative to `sp`, and push or pop by decrementing/incrementing `sp`.

### Calling the next function

The only tricky bit is what the function should do when it's done. It can't just return: Unlike most interpreters, Tails doesn't have some "inner loop" that deciphers the code and calls primitive functions one after the other. Instead, _each primitive function is responsible for continuing the interpreter._ 

**Wait! How do I know what the interpreter should do next?! I'm getting stage fright!**

You just need to read the next `Instruction` from `*pc` and call it. `Instruction` is a `union` type, but its primary member `native` is simply a function pointer, whose signature is exactly that of a primitive function (shown above.) 

Got it? To call the next function, you have to get that function pointer and call it with the current stack pointer `sp` and the new program counter `pc+1`.

That function will return the updated stack pointer, and then ... well, there's nothing more to do. You just return it.

If you put all this together, you find that the code to put at the end of a native function is:
```c
    return pc->native(sp, pc + 1);
```
Since this occurs at the end of every function, it's abstracted into a macro called `NEXT()`, which is the traditional Forth name for this.

**Every function just calls the next one?! Doesn't the stack just keep growing till it overflows?**

The stack doesn't grow, because of _tail call optimization_: if a function ends with a call to another function and simply returns that function's result, the assembly subroutine-call and return instructions can be replaced by a simple jump to the other function. Pretty much every C/C+\+ compiler does this when optimizations are enabled.

> Note: In _non-optimized_ (debug) builds, the stack does grow. To work around this, Tails attempts to use a compiler attribute called `musttail` that forces the compiler to tail-call-optimize even when optimizations aren't on. Currently [May 2021] this is only available in Clang, and won't be in a release until Clang 13. Until then, don't let your tests don't run so long that they blow the stack!

**When does this ever return? Is it just an infinite regress?**

The function finally returns to its caller when one of the subsequent functions being called _doesn't_ end with `NEXT()`. There is one primitive that does that, aptly named `RETURN`. Its body consists simply of `return sp;`.

**Got it.**

Excellent. Now you know how the interpreter works: 

* You're given a C array of `Instruction` containing pointers to primitive functions, the last of which is `RETURN`.
* Call the first one in the list. You pass it `sp` pointing to the end of a sufficiently large array of `int`, and `pc` pointing to the next (second) function pointer. 
* When it returns, you can find the results on the stack at the new `sp` value it returned.

### Interpreted functions

**But I want to write functions in Forth, not in C+\+.**

Yeah, Tails wouldn't be much of a language if you couldn't define functions in it, so there has to be a way to call a non-native word! This is done by the `CALL` primitive, which gets the address of an interpreted Forth word (i.e. a list of function pointers, as above) and calls the first function pointer in that list.

**But where does the CALL function get the address of the word to call?**

It reads it from the instruction stream, as `pc->word`, which is a pointer to an `Instruction` not a function pointer. (Remember that `pc` points to a `union` value.) Then it increments `pc` to skip over the extra value it just read:

```c
    int* CALL(int *sp, const Instruction *pc) {
        sp = call(sp, (pc++)->word);
        NEXT();
    }
```

So when laying out some code to run, if you want to call a word implemented in Forth, you just add the primitive `CALL` followed by the address of the word. Easy.

### Threading

In Forth, "threading" isn't about concurrency; it refers to the unusual way the interpreter runs, as I just finished describing. "[Threaded code][THREADED]" consists of a list of "word" (function) pointers, which the interpreter dispatches to one after the other. It's like the functions are "threaded together" by the list of pointers.

Tails uses "direct threading", where each address in the code is literally a pointer to a native word. That makes dispatching as fast as possible. The downsides are that non-native Forth words are somewhat slower to call, and take up twice as much space in the code. I figured this was a good trade-off; the use cases I have in mind involve mostly running native words, kind of like a traditional interpreter with a fixed set of opcodes.

The other main approach is called "indirect threading", where there's another layer of indirection between the pointers in a word and the native code to run. This adds overhead to each call, especially on modern CPUs where memory fetches are serious bottlenecks. But it's more flexible. It's described in detail in the article linked above.

> Tip: A great resource for learning how a traditional Forth interpreter works is [JonesForth][JONES], a tiny interpreter in x86 assembly written in "literate" style, with almost as much commentary as code.

## Performance

Tails was directly inspired by [the design of the Wasm3 interpreter][WASM3INTERP], which cleverly uses tail calls and register-based parameter passing to remove most of the overhead of C, allowing the code to be nearly as optimal as hand-written assembly. I realized this would permit the bootstrap part of a Forth interpreter -- the part that implements the way words (functions) are called, and the primitive words -- to be written in pure C or C+\+.

It did turn out to be as efficient as Steven Massey promised, though only with the proper build flags applied to the primitives. For Clang, and I think also GCC, they are:

> `-O3 -fomit-frame-pointer -fno-stack-check -fno-stack-protector`

The first flag enables optimizations so that tail calls become jumps; the other flags disable stack frames, so functions don't unnecessarily push and pop to the native stack.

For example, here's the x86-64 assembly code of the PLUS function, compiled by Clang 11. Since the platform calling conventions use registers for the initial parameters and the return value, there's no use of the C stack at all!
```
3cd0    movl    (%rdi), %eax        ; load top of data stack into eax
3cd2    addl    %eax, 0x4(%rdi)     ; add eax to second data stack item
3cd5    addq    $0x4, %rdi          ; pop the data stack
3cd9    movq    (%rsi), %rax        ; load next word pointer into rax
3cdc    addq    $0x8, %rsi          ; bump the program counter
3ce0    jmpq    *%rax               ; jump to the next word
```

## To-Do List

* Add memory that programs can read/write
* Other data types, like strings?
* Stack bounds checking
* Define a bunch more core words in native code
* Define the all-important `:` and `;` words, so words can be defined in Forth itself...

[FORTH]: https://en.wikipedia.org/wiki/Forth_(programming_language)
[WASM]: https://webassembly.org
[THREADED]: http://www.complang.tuwien.ac.at/forth/threaded-code.html
[JONES]: https://github.com/nornagon/jonesforth/blob/master/jonesforth.S
[WASM3]: https://github.com/wasm3/wasm3
[WASM3INTERP]: https://github.com/wasm3/wasm3/blob/main/docs/Interpreter.md#m3-massey-meta-machine
