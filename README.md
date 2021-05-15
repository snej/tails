# Tails, A Fast C+\+ Forth Core

**Tails** is a minimal, fast [Forth][FORTH]-like interpreter core written entirely in C+\+. I created it as a one-day hack to celebrate May Forth 2021 … then kept going because it's fun.

What can it do? Not much on its own. It knows how to add and multiply integers!! and call functions!!!! It can evaluate `4 3 + SQUARE DUP + SQUARE ABS` and return the expected answer `9604`. That expression can be written as a hardcoded list of word references, or parsed from a string.

It's not much, but it's pretty tiny! The magic core code (`NEXT`, `CALL`, `RETURN`, `LITERAL`, `DUP`, etc.) is about 200SLOC and compiles to a few hundred bytes, many of which are NOPs the compiler adds for padding. The parser and compiler add a few KB more.
 
And it's very easy to extend -- see the to-do list at the end.

The reason I wrote this, besides to fulfil a decades-old dream of building a working Forth interpreter, is to apply the really elegant and efficient implementation technique used by [Wasm3][WASM3] (a [WebAssembly][WASM] interpreter), which is probably the fastest pure non-JIT interpreter there is. See below under "Performance".
 
## Just Show Me The Code!

* The absolute core is [instruction.hh](https://github.com/snej/tails/blob/main/src/instruction.hh), which defines how code is structured and called, and how one primitive/native word proceeds to the next. (It uses a class `Value` that represents the items on the stack; for now you can just pretend that's a typedef for `int` or `double`.)
* [word.hh](https://github.com/snej/tails/blob/main/src/word.hh) defines the `Word` class that associates a name and flags with an (interpreted or primitive) function.
* [core_words.cc](https://github.com/snej/tails/blob/main/src/core_words.cc) defines the very basic primitives like `LITERAL`, `DUP`, `+`, and some interpreted words like `ABS` and `MAX`.
 
## Theory Of Operation

Tails's world is currently very simple: a stack of values, a call stack, and a program consisting of an array of function pointers. It's "the simplest thing that could possibly work", but it gets you quite a lot.

### Core (AKA native, primitive) functions

Tails's core primitives are C+\+ functions of the form `int* FN(Value *sp, const Instruction *pc)`.

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
Since this occurs at the end of every native function, it's abstracted into a macro called `NEXT()`, which is the traditional Forth name for this.

**Every function just calls the next one?! Doesn't the stack just keep growing till it overflows?**

The stack doesn't grow, because of _tail call optimization_: if a C/C+\+ function ends with a call to another function and simply returns that function's result, the compiler can replace the 'call' and 'return' machine instructions with a simple jump to the other function. Pretty much every C/C+\+ compiler does this when optimizations are enabled.

> Note: In _non-optimized_ (debug) builds, the stack does grow. To work around this, Tails attempts to use a compiler attribute called `musttail` that forces the compiler to tail-call-optimize even when optimizations aren't on. Currently [May 2021] this is only available in Clang, and won't be in a release until Clang 13. Until then, don't let your tests don't run so long that they blow the stack!

**When does this ever return? Is it just an infinite regress?**

The function finally returns to its caller when one of the subsequent functions being called _doesn't_ end with `NEXT()`. There is one primitive that does that, aptly named `RETURN`. Its body consists simply of `return sp;`.

**Got it.**

Excellent. Now you know how the interpreter works: 

* You're given a C array of `Instruction` containing pointers to primitive functions, the last of which is `RETURN`.
* Call the first one in the list. You pass it `sp` pointing to the end of a sufficiently large array of `Value`, and `pc` pointing to the next (second) function pointer. 
* When it returns, you can find the results on the stack at the new `sp` value it returned.

### Interpreted functions

**But I want to write functions in Forth, not in C+\+.**

Yeah, Tails wouldn't be much of a language if you couldn't define functions in it, so there has to be a way to call a non-native word! This is done by the `CALL` primitive, which gets the address of an interpreted Forth word (i.e. a list of function pointers, as above) and calls the first function pointer in that list.

**But where does the CALL function get the address of the word to call?**

It reads it from the instruction stream, as `pc->word`, which is a pointer to an `Instruction` not a function pointer. (Remember that `pc` points to a `union` value.) Then it increments `pc` to skip over the extra value it just read:

```c
    int* CALL(Value *sp, const Instruction *pc) {
        sp = call(sp, (pc++)->word);
        NEXT();
    }
```

So when laying out some code to run, if you want to call a word implemented in Forth, you just add the primitive `CALL` followed by the address of the word. Easy.

>Note: There are a few other primitive words that use the same trick of storing parameters in the instruction stream. `LITERAL` stores the literal Value to push, and `BRANCH` and `ZBRANCH` store the pc offset.

### Threading

In Forth, "threading" isn't about concurrency; it refers to the unusual way the interpreter runs, as I just finished describing. "[Threaded code][THREADED]" consists of a list of "word" (function) pointers, which the interpreter dispatches to one after the other. It's like the functions are "threaded together" by the list of pointers.

Tails uses "direct threading", where each address in the code is literally a pointer to a native word. That makes dispatching as fast as possible. The downsides are that non-native Forth words are somewhat slower to call, and take up twice as much space in the code. I figured this was a good trade-off; the use cases I have in mind involve mostly running native words, kind of like a traditional interpreter with a fixed set of opcodes.

The other main approach is called "indirect threading", where there's another layer of indirection between the pointers in a word and the native code to run. This adds overhead to each call, especially on modern CPUs where memory fetches are serious bottlenecks. But it's more flexible. It's described in detail in the article linked above.

> Tip: A great resource for learning how a traditional Forth interpreter works is [JonesForth][JONES], a tiny interpreter in x86 assembly written in "literate" style, with almost as much commentary as code.

## Compilation

There are several ways to add words to Tails:

1. **Define a native word in C++ using the `NATIVE_WORD` macro.** It receives the stack pointer in `sp` and the interpreter program counter (from which it can read parameters) in `pc`. It must end with a call to the `NEXT()` macro. The word is declared as a `constexpr Word` object that can be referenced in later definitions.
2. **Define an interpreted word at (native) compile time**, using the `INTERP_WORD` macro. This is very low level, not like regular Forth syntax but just a sequence of `Word` symbols defined by earlier uses of `NATIVE_WORD` and `INTERP_WORD`. A `RETURN` will be added at the end. In particular:
   * A literal value has to be written as `LITERAL` followed by the number/string
   * A call to an interpreted word has to be written as `CALL` followed by the word
   * Control flow has to be done using `BRANCH` or `ZBRANCH` followed by the offset
3. **Compile a word at runtime**, using the `CompiledWord` subclass. It can be given a list of `Word` symbols, or it can parse a string. The string mode is by no means a full Forth REPL, just a C++ hack, but it supports `IF`, `ELSE` and `THEN` for control flow.

There are examples of 1 and 2 in `core_words.cc`, and of 3 in `test.cc`.

### Stack Effects

There's a convention in Forth of annotating a word's definition with a "stack effect" comment that shows the input values it expects to find on the stack, and the output values it leaves on the stack. Some Forth-family languages like [Factor][FACTOR] make these part of the language, and statically check that the actual behavior of the word matches. This is very useful, since otherwise mistakes in stack depth are easy to make and hard to debug!

Tails's `CompiledWord` class computes the stack effect of a word as it compiles it. If the code isn't self-consistent, like if one branch of an `IF` has a different effect than the other, it fails with an error. It also computes the maximum depth of the stack while it's doing this. The stack effect is available as part of the metadata of the word.

A top-level interpreter can use this to allocate a minimally-sized stack and verify that it won't overflow or underflow. Therefore no stack checks are needed at runtime! The `run` function in `test.cc` demonstrates this: It won't run a word whose stack effect's `input` is nonzero, because the stack would underflow, or whose `output` is zero, because there wouldn't be any result left on the stack. And it uses the stack effect's `max` as the size of stack to allocate.

>Warning: The  `NATIVE_WORD` and `INTERP_WORD` macros are **not** smart enough to check stack effects. When defining a word with these you have to give the word's stack effect, on the honor system; if you get it wrong, `CompiledWord` will get wrong results for words that call that one. G.I.G.O.!

## Runtime

### Data Types

The `Value` class defines what Tails can operate on. The stack is just a C array of `Value`s. There are currently two `Value` implementations, available at the flip of a `#define`:

**The simple Value** is just a trivial wrapper around a `double`, so all it supports are numbers.

**The fancy Value** uses the so-called "[NaN tagging][NAN]" or "Nan boxing" trick that's used by several dynamic language runtimes, such as LuaJIT and the WebKit and Mozilla JavaScript VMs. It currently supports `double`s and strings, but is extensible.

There is unfortunately no garbage collector or ref-counting yet, so heap-allocated strings are simply leaked. Fortunately the fancy Value can store strings up to six bytes long inline, which reduces waste.

### Performance

Tails was directly inspired by [the design of the Wasm3 interpreter][WASM3INTERP], which cleverly uses tail calls and register-based parameter passing to remove most of the overhead of C, allowing the code to be nearly as optimal as hand-written assembly. I realized this would permit the bootstrap part of a Forth interpreter -- the part that implements the way words (functions) are called, and the primitive words -- to be written in pure C or C+\+.

It turned out to be as efficient as Steven Massey promised, though you do need a specific set of build flags applied to the primitives. For Clang, and I think also GCC, they are:

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

* Add more data types, like arrays and dictionaries.
* Define a bunch more core words.
* Define the all-important `:` and `;` words, so words can be defined in Forth itself.
* Type checking in stack effects? (E.g. declare that a word takes two strings and returns a boolean.)

[FORTH]: https://en.wikipedia.org/wiki/Forth_(programming_language)
[WASM]: https://webassembly.org
[THREADED]: http://www.complang.tuwien.ac.at/forth/threaded-code.html
[JONES]: https://github.com/nornagon/jonesforth/blob/master/jonesforth.S
[WASM3]: https://github.com/wasm3/wasm3
[WASM3INTERP]: https://github.com/wasm3/wasm3/blob/main/docs/Interpreter.md#m3-massey-meta-machine
[FACTOR]: https://factorcode.org
[NAN]: https://www.npopov.com/2012/02/02/Pointer-magic-for-efficient-dynamic-value-representations.html
