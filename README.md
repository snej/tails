# Tails, A Fast C+\+ Forth Core

**Tails** is a minimal, fast [Forth][FORTH]-like interpreter core. It uses no assembly code, only C+\+, but an elegant tail-recursion technique inspired by [Wasm3][WASM3] makes it nearly as efficient as hand-written assembly. I created it as a one-day hack to celebrate May Forth 2021 … then kept going because it's fun.

It started out as tiny but functional. The magic core code (`NEXT`, `INTERP`, `RETURN`, `LITERAL`, `DUP`, etc.) is about 200SLOC and compiles to a few hundred bytes, many of which are NOPs the compiler adds for padding. The parser and compiler add a few KB more.

It's grown significantly since then: there's a parser; a stack-checking & type-checking compiler; multiple value types including strings and arrays; "quotations" (i.e. lambdas or blocks); garbage collection ... but the simple core can still be extracted and used if something more minimal is needed.

Tails doesn't follow the usual Forth implementation strategy of starting with a minimal assembly-language core and then building as much as possible in Forth itself. That makes sense for a system where you're going to be writing applications entirely in Forth; but for my purposes I'm more interested in having an embedded language to use for small tasks inside an application written in a more traditional compiled language like C++.

## 0. TL;DR

### Just Show Me The Code!

* The absolute core is [instruction.hh](https://github.com/snej/tails/blob/main/src/core/instruction.hh), which defines how code is structured and called, and how one primitive/native word proceeds to the next. (It uses a class `Value` that represents the items on the stack; for now you can just pretend that's a typedef for `int` or `double`.)
* [word.hh](https://github.com/snej/tails/blob/main/src/core/word.hh) defines the `Word` class that associates a name and flags with an (interpreted or primitive) function.
* [core_words.cc](https://github.com/snej/tails/blob/main/src/core/core_words.cc) defines the very basic primitives like `LITERAL`, `DUP`, `+`, and some interpreted words like `ABS` and `MAX`.

### Just Show Me The Language!

Here's an [overview of the syntax and vocabulary][SYNTAX].

## 1. Why Another Forth?

I think "build a working Forth interpreter" has been on my bucket list for a while. As a teenager circa 1980, I had an Apple II and wrote a lot of programs in BASIC. I got frustrated by its slowness, but 6502 assembly (which most games were written in) was quite nasty. Then I got FIG-Forth, which was kind of a Goldilocks language -- a lot faster than BASIC, with direct access to memory and hardware, but much friendlier than assembly. I got ahold of a printed listing of FIG-Forth (the Z80 version, for some reason) and worked my way through it, learning a lot. Then in college I tried to write my own interpreter for a 68000-based workstation, but never got it debugged. Now I'm trying again, but starting from a higher level.

A more recent reason, and more practical, is to have a simple but fast interpreter to use for things like implementing database queries. Queries can do very general-purpose things like evaluating arbitrary arithmetic and logical expressions, and they also have complex higher-level logic for traversing indexes. They're usually translated into an interpreted form. I wrote such a query interpreter last year for a database project, but was unhappy with the implementation.

The final reason, the immediate impetus, was to apply the really elegant and efficient implementation technique used by [Wasm3][WASM3] (a [WebAssembly][WASM] interpreter), which is probably the fastest pure non-JIT interpreter there is. See below under "Performance".
 
## 2. Theory Of Operation

Tails's world is currently very simple: a stack of (dynamically-typed) values, a call stack, and a program consisting of an array of function pointers. It's "the simplest thing that could possibly work", but it gets you quite a lot.

### Core (AKA native, primitive) functions

Tails's core primitives are C+\+ functions of the form `Value* FN(Value *sp, const Instruction *pc)`.

* `sp` is the stack pointer. It grows upward, so `sp[0]` is the top value, `sp[-1]` is below it, etc.
* `pc` is the program counter, which points to an array of `Instruction`s, specifically to the next instruction to execute.
* The return value is the updated stack pointer.

The function body can read and write values on the stack relative to `sp`, and push or pop by incrementing/decrementing `sp`.

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
* Call the first one in the list. You pass it `sp` pointing to the start of a sufficiently large array of `Value`, and `pc` pointing to the next (second) function pointer. 
* When it returns, you can find the result(s) on the stack at the new `sp` value it returned.

### Interpreted functions

**But I want to write functions in Forth, not in C+\+.**

Yeah, Tails wouldn't be much of a language if you couldn't define functions in it, so there has to be a way to call a non-native word! This is done by the `INTERP` primitive, which gets the address of an interpreted Forth word (i.e. a list of function pointers, as above) and calls the first function pointer in that list.

>Note: It used to be called `CALL`, but that conflicts with the name of a higher-level word in Factor (and some other Forths?) to call a "quotation" (inline anonymous function.)

**But where does INTERP get the address of the word to call?**

It reads it from the instruction stream, as `pc->word`, which is a pointer to an `Instruction` not a function pointer. (Remember that `pc` points to a `union` value.) Then it increments `pc` to skip over the extra value it just read:

```c
    Value* INTERP(Value *sp, const Instruction *pc) {
        sp = call(sp, (pc++)->word);
        NEXT();
    }
```

So when laying out some code to run, if you want to call a word implemented in Forth, you just add the primitive `INTERP` followed by the address of the word. Easy.

>Note: There are a few other primitive words that use the same trick of storing parameters in the instruction stream. `LITERAL` stores the literal Value to push, and `BRANCH` and `ZBRANCH` store the pc offset.

### Threading

In Forth, "threading" isn't about concurrency; it refers to the unusual way the interpreter runs, as I just finished describing. "[Threaded code][THREADED]" consists of a list of "word" (function) pointers, which the interpreter dispatches to one after the other. It's like the functions are "threaded together" by the list of pointers.

Tails uses "direct threading", where each address in the code is literally a pointer to a native word. That makes dispatching as fast as possible. The downsides are that non-native Forth words are somewhat slower to call, and take up twice as much space in the code. I figured this was a good trade-off; the use cases I have in mind involve mostly running native words, kind of like a traditional interpreter with a fixed set of opcodes.

The other main approach is called "indirect threading", where there's another layer of indirection between the pointers in a word and the native code to run. This adds overhead to each call, especially on modern CPUs where memory fetches are serious bottlenecks. But it's more flexible. It's described in detail in the article linked above.

> Tip: A great resource for learning how a traditional Forth interpreter works is [JonesForth][JONES], a tiny interpreter in x86 assembly written in "literate" style, with almost as much commentary as code.

### Optimizations

There are some variants of the core word `INTERP` that aren't strictly necessary but allow faster and more compact code.

`INTERP2` is followed by _two_ interpreted word pointers, and calls them sequentially. There are also `INTERP3` and `INTERP4`; there could be any number of these. The compiler uses these when there are two or more consecutive calls to interpreted words. They make the code smaller by omitting one or more `CALL` instructions, and they also save the time needed to go into and out of those instructions.

`TAILINTERP` _jumps_ to the interpreted word, instead of making a regular call and then doing `NEXT`. It's the tail-call optimization, applied to interpreted code. The compiler emits this as a replacement for `INTERP` followed by `RETURN`. Not only is it faster, it also prevents the return stack from growing, saving memory and making recursive algorithms more practical.

And yes, there exist `TAILINTERP2`, `TAILINTERP3`, `TAILINTERP4`.

>Note: Of course there's nothing magic about 4; there could be any number of words in this family. In real usage it would probably be beneficial to go at least up to 8, maybe higher.

Another optimization is inlining. The "inline" flag in an interpreted word's metadata is a hint to the compiler to insert its instructions inline instead of emitting a call. It turns out that inlining is pretty trivial to implement in a concatenative (stack-based) language: you literally just copy the contents of the word, stopping before the `RETURN`.

### Recursion

Recursion is tricky in most Forths, simply because the word you're defining doesn't yet have a name you can call it by; it isn't registered in the vocabulary until the definition is complete. Tails addresses this with a special word `RECURSE`, which recursively calls the current word.

Recursion doesn't use `INTERP`; instead `RECURSE` is followed by a relative offset back to the start of the word. It's sort of a hybrid of `BRANCH` and `INTERP` in that it uses a PC offset but creates a new call-stack frame.

Of course Tails supports tail-call optimization for recursive words! If `RECURSE` is followed by `RETURN`, or a `BRANCH` to `RETURN`, it's replaced by a simple `BRANCH` ... so the recursion is reduced to a loop.

## 3. Compilation

There are several ways to add words to Tails:

1. **Define a native word in C++ using the `NATIVE_WORD` macro.** It receives the stack pointer in `sp` and the interpreter program counter (from which it can read parameters) in `pc`. It must end with a call to the `NEXT()` macro. The word is declared as a `constexpr Word` object that can be referenced in later definitions.
2. **Define an interpreted word at (native) compile time**, using the `INTERP_WORD` macro. This is very low level, not like regular Forth syntax but just a sequence of `Word` symbols defined by earlier uses of `NATIVE_WORD` and `INTERP_WORD`. A `RETURN` will be added at the end. In particular:
   * A literal value has to be written as `LITERAL` followed by the number/string
   * A call to an interpreted word has to be written as `INTERP` followed by the word
   * Control flow has to be done using `BRANCH` or `ZBRANCH` followed by the offset
3. **Compile a word at runtime**, using the `Compiler` class. The usual way to invoke it is to give it a string of source code to parse. This is not yet a full Forth parser, but it supports `IF`, `ELSE`, `THEN`, `BEGIN`, `WHILE`, `LOOP` for basic control flow.

There are examples of 1 and 2 in `core_words.cc`, and of 3 in `test.cc` and `repl.cc`

### The Parser

`Compiler::parse` is a basic parser implemented in C++. It reads tokens as:

- A double-quoted string
- A numeral, via `strtod`, so it understands decimal, hex, and scientific notation in C syntax.
- An open or close square- or curly-bracket
- The soecial control words `IF`, `ELSE`, `THEN`, `BEGIN`, `WHILE`, `LOOP`
- Anything else is looked up as the name of an already-defined word

Strings and numbers are added as literals. Braces delimit arrays of literals, and brackets delimit nested words ("quotations") that are also compiled as literals. An ordinary word adds a call to that word.

The control-flow words invoke hardcoded functionality that ends up emitting magic `ZBRANCH` and `BRANCH` words.

This is not as clean as a regular Forth compiler, which is written in Forth, and which has an ingenious system of "immediate" words that implement soecial compilation. In my defense, (a) this is for bringup, and (b) for my own purposes, making Tails self-hosting is not a high priority.

### Interactive Interpreter (REPL)

The source file `repl.cc` implements a simple interactive mode that lets you type in words and run them. After each line it shows the current stack.

You cannot yet define words in this mode; there's no "`:`" word yet.

### Stack Effects

There's a convention in Forth of annotating a word's definition with a "stack effect" comment that shows the input values it expects to find on the stack, and the output values it leaves on the stack. Some Forth-family languages like [Factor][FACTOR] make these part of the language, and statically check that the actual behavior of the word matches. This is very useful, since otherwise mistakes in stack depth are easy to make and hard to debug!

Tails enforces stack effects. It even uses them for type checking. The stack effect can be given explicitly when a word is compiled, or it can be inferred from the code. Either way, the compiler runs a stack checker that simulates the stack as the word runs. If a stack effect has already been given, the stack checker will notice any deviation from it and flag a compile error.  Otherwise, the resulting effect at the end of the word becomes the word's stack effect. The stack effect is saved as part of the word's metadata, and used when compiling later words that call it.

The checker also rejects mismatched parameter types (when a type on the stack doesn't match the input stack effect of the word being called), and inconsistent stack depths in the two branches of an `IF`...`ELSE`.

During this, stack checker also also tracks the maximum depth of the stack, and saves that as part of the stack effect. A top-level interpreter can use this to allocate a minimally-sized stack and know that it won't overflow or underflow. Therefore **no stack checks are needed at runtime!** The `run` function in `test.cc` demonstrates this: It won't run a word whose stack effect's `input` is nonzero, because the stack would underflow, or whose `output` is zero, because there wouldn't be any result left on the stack. And it uses the stack effect's `max` as the size of stack to allocate.

>Warning: The  `NATIVE_WORD` and `INTERP_WORD` macros are **not** smart enough to check stack effects. When defining a word with these you have to give the word's stack effect, on the honor system; if you get it wrong, `CompiledWord` will get wrong results for words that call that one. G.I.G.O.!

There are a couple of primitives whose stack effects are not fixed. These words are given a special flag called "Weird". The stack checker has hardcoded handlers for such words, as described below...

#### Stack Checking Vs. Recursion

The `RECURSE` word throws a few wrenches into the stack checking machinery.

**First,** if the current word's stack effect hasn't been given explicitly, and the compiler is determining it as it goes along by tracing the flow of control, then it doesn't actually know what the stack effect of a recursive call is. (Or at least, I haven't put enough thought into figuring out whether or in what cases it could know.) So the compiler will fail with an error in this case. In other words, when defining a recursive word, you have to give its stack effect up front.

**Second,** a recursive function's maximum stack depth can't be determined at compile time. Recursive functions can use unbounded or even infinite stack space (both data and call stacks.) Trying to statically analyze the code to determine the maximum recursion depth is equivalent to the [Halting Problem][HALTING], i.e. impossible. So Tails gives up: the `RECURSE` word is considered to have _infinite_ maximum stack depth, where by "infinite" we mean 65535, and this is propagated to the word that calls it. So in practice, the runtime will allocate a pretty large stack when running recursive code, which is usually sufficient.

>Note: This doesn't apply to tail recursion. A tail-recursive word's stack effect can be determined normally, so it's finite. (A word that grew the stack before tail-recursing, like `{DUP RECURSE}`, would be rejected by the regular stack checker.)

>Note: A future optimization could be to have the `RECURSE` primitive check the free space in the data stack, and (somehow) grow the stack if necessary.

#### Stack Checking Vs. Quotations

The `CALL` primitive which invokes a quotation has a stack effect that's indeterminate, because its effect is that of the quotation object on top of the stack, which in general isn't known at compile time.

Factor calls this situation "row polymorphism" and has a complex type of stack effect declaration to express it. I'm still trying to figure out how it works and how it could be implemented.

For now I've put in a simple kludge. Words with variable stack effects have a special flag called "Weird". There are currently three such words: the primitives `CALL`, `IFELSE` and `RECURSE`. The stack checker rejects such a word unless it has a hardcoded handler for it. The handler for `IFELSE` requires that the preceding two words are quote literals, with equivalent stack effects, and uses that effect.

I hope to replace this with a more general and elegant mechanism soon. In the meantime, this means that the only use for quotes is with `IFELSE`. Sorry!

## 4. Runtime

### Data Types

The `Value` class defines what Tails can operate on. The stack is just a C array of `Value`s. There are currently two `Value` implementations, available at the flip of a `#define`:

**The simple Value** is just a trivial wrapper around a `double`, so all it supports are numbers.

**The fancy Value** uses the so-called "[NaN tagging][NAN]" or "Nan boxing" trick that's used by several dynamic language runtimes, such as LuaJIT and the WebKit and Mozilla JavaScript VMs. It currently supports `double`s, strings, arrays and Words (quotations), and is extensible. It has a very simple garbage collector.

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

#### Register usage in function calls

X86-64, Unix and Apple platforms:

>"The first six integer or pointer arguments are passed in registers RDI, RSI, RDX, RCX, R8, R9 (R10 is used as a static chain pointer in case of nested functions), while XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6 and XMM7 are used for the first floating point arguments. As in the Microsoft x64 calling convention, additional arguments are passed on the stack. Integer return values up to 64 bits in size are stored in RAX while values up to 128 bit are stored in RAX and RDX. Floating-point return values are similarly stored in XMM0 and XMM1. The wider YMM and ZMM registers are used for passing and returning wider values in place of XMM when they exist." --[Wikipedia](https://en.wikipedia.org/wiki/X86_calling_conventions#System_V_AMD64_ABI)

X86-64, Windows:

>"The first four arguments are placed onto the registers. That means RCX, RDX, R8, R9 for integer, struct or pointer arguments (in that order), and XMM0, XMM1, XMM2, XMM3 for floating point arguments. Additional arguments are pushed onto the stack (right to left). Integer return values (similar to x86) are returned in RAX if 64 bits or less. Floating point return values are returned in XMM0. Parameters less than 64 bits long are not zero extended; the high bits are not zeroed. If the callee wishes to use registers RBX, RSP, RBP, and R12–R15, it must restore their original values before returning control to the caller. All other registers must be saved by the caller if it wishes to preserve their values." --[Wikipedia](https://en.wikipedia.org/wiki/X86_calling_conventions#Microsoft_x64_calling_convention)

ARM64:

ARM64 calling conventions are more standardized, but the official specification is extremely convoluted. In a nutshell, the first eight int/struct/pointer arguments are passed in registers x0–x7. The return value goes in x0 (up to 64 bits), or in x0 and x1 (up to 128 bits); if larger, the caller sets x8 to the location to write the result. (See this handy [ARM64 cheat sheet](https://github.com/Siguza/ios-resources/blob/master/bits/arm64.md#calling-convention).)


[FORTH]: https://en.wikipedia.org/wiki/Forth_(programming_language)
[WASM]: https://webassembly.org
[THREADED]: http://www.complang.tuwien.ac.at/forth/threaded-code.html
[JONES]: https://github.com/nornagon/jonesforth/blob/master/jonesforth.S
[WASM3]: https://github.com/wasm3/wasm3
[WASM3INTERP]: https://github.com/wasm3/wasm3/blob/main/docs/Interpreter.md#m3-massey-meta-machine
[FACTOR]: https://factorcode.org
[NAN]: https://www.npopov.com/2012/02/02/Pointer-magic-for-efficient-dynamic-value-representations.html
[HALTING]: https://en.wikipedia.org/wiki/Halting_problem
[SYNTAX]: https://github.com/snej/tails/blob/main/Syntax.md
