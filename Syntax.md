# Tails Syntax

The syntax of Tails is quite simple:

* A line of input is broken into tokens. Tokens are mostly separated by whitespace, but the quote and bracket characters that mark string, array and quotation literals are also token boundaries.
* A token matching a literal _(q.v.)_ pushes that literal value on the stack.
* `IF`, `THEN`, `ELSE`, `BEGIN`, `WHILE`, `REPEAT` are reserved words for control structures, as described below.
* Anything else is looked up as a word (function) in the vocabulary, and evaluates that word.
* If a token doesn't match anything, it's a syntax error.

> NOTE: All name lookup is **case-insensitive**, at least for ASCII letters.


## 1. Literals

| Type   | Syntax | Examples |
|--------|--------|----------|
| Null   | `NULL`                                 | `NULL`, `null`, `Null` |
| Number | Same as C                              | `42`, `-8`, `0xbeef`, `1.234`, `6.02e23` |
| String | Double-quoted, backslash escape        | `"foo"`, `""`, `"\"wow\""` |
| Array  | Square brackets, space-separated       | `[1 2 NULL "foo"]`, `[]` |
| Quotation | Curly braces; optional stack effect | `{DUP *}`, `{(# # -- #) DUP +}` |

Quotations are described in more detail below, under **Defining New Words**.


## 2. Built-In Words

| Name   | Inputs | Outputs | Description |
|--------|--------|---------|-------------|
| `DROP` | a      |         | Remove top value from stack |
| `DUP`  | a      | a a     | Duplicates top value |
| `OVER` | a b    | a b a   | Copies 2nd value to top |
| `ROT`  | a b c  | b c a   | Moves 3rd value to top |
| `SWAP` | a b    | b a     | Swaps top two values |
| `+`    | a b    | c       | Addition, or string or array concatenation |
| `-`    | # #    | #       | Subtraction |
| `*`    | # #    | #       | Multiplication |
| `/`    | # #    | #       | Division |
| `=`    | a b    | #       | Outputs 1 if a=b, else 0 |
| `<>`   | a b    | #       | Outputs 1 if a≠b, else 0 | 
| `>`    | a b    | #       | Outputs 1 if a>b, else 0 | 
| `>=`   | a b    | #       | Outputs 1 if a>b, else 0 | 
| `<`    | a b    | #       | Outputs 1 if a\<b, else 0 | 
| `<=`   | a b    | #       | Outputs 1 if a\<=b, else 0 | 
| `0=`   | a      | #       | Outputs 1 if a=0, else 0 |
| `0<>`  | a      | #       | Outputs 1 if a≠0, else 0 |
| `0>`   | a      | #       | Outputs 1 if a>0, else 0 |
| `0<`   | a      | #       | Outputs 1 if a\<0, else 0 |
| `LENGTH`| a     | #       | Length of string or array |
| `ABS`  | #      | #       | Absolute value |
| `MAX`  | a b    | max     | Maximum of a, b |
| `MIN`  | a b    | min     | Minimum of a, b |
| `.`    | a      |         | Writes text representation of `a` to stdout. |
| `SP.`  |        |         | Writes a space character to stdout. |
| `NL.`  |        |         | Writes a newline to stdout. |
| `NL?`  |        |         | Writes a newline, if necessary to start a new line. |
| `DEFINE`| quote name |    | Registers `quote` as a new word named `name`. |
| `CALL` | ... quote| ?     | Evaluates a quotation _(can't be used directly yet)_ |


## 3. Control Structures

("Truthy" means any value but `0` or `NULL`.)

| Type        | Syntax        | Description |
|-------------|---------------|------------|
| Conditional | `IF ... THEN` | `IF` pops a value; if it's truthy, evaluates the words before `THEN`. |
|             | `IF ... ELSE ... THEN` | `IF` pops a value; if it's truthy, evaluates the words before `THEN`, else evaluates words before `ELSE`.  |
|             | `a {...} {...} IFELSE` | Pops 3 params. If `a` is truthy calls the first quote, else calls the second.
| Loop        | `BEGIN ... WHILE ... REPEAT` | `WHILE` pops a value, jumps past `REPEAT` if it's zero/null. `REPEAT` jumps back to `BEGIN`. |
| Recursion   | `RECURSE`    | Calls the current word recursively. |


## 4. Defining New Words

A quotation is a value that's a function, i.e. a sequence of words to evaluate. The only difference between a quotation and a word is that a word is registered in a vocabulary for the parser to find.

To define a word, just write a quotation literal, then the name as a string, then invoke `DEFINE`:

    { .... } "name" DEFINE
    
or, with a _stack effect_ declaration:

    { (... -- ...) .... } "name" DEFINE


### Stack Effects

Every word and quotation has a stack effect, which declares the number of input and output values, and optionally their types. The compiler uses this to ensure that the stack doesn't get unbalanced, and can't underflow or overflow, and that there aren't any type mismatches. (Yes, words with variable numbers of inputs or outputs are not allowed.)

Declaring a stack effect for a quotation is optional; if you don't, the compiler will figure it out on its own by examining the stack effects of the words the quotation calls. But in a quotation that defines a word it's good to declare it up front; both as human readable documentation, and so that the compiler can check your work and give you an error if the actual effect doesn't match the declaration.

A stack effect declaration follows the opening `{` of a quotation and consists of:

1. An open paren `(`
2. Zero or more tokens, each representing an input value on the stack, with the top of stack on the right.
3. A separator `--`
4. Zero or more tokens for output values, with top of stack on the right.
5. A close paren `)`

The input/output tokens can contain ASCII letters, underscores, and these punctuation marks that denote specific types, in no particular order:

| Mark        | Type |
|-------------|------|
| `?`         | Null |
| `#`         | Number |
| `$`         | String |
| `[` or `]`  | Array |
| `{` or `}`  | Quote |

A token with no punctuation marks represents a value of _any type_. Otherwise it represents only the given type(s). So for example `foo` can be any type, while `[foo]#?` can only be a number, an array or null.

If **an output token exactly matches an input token**, that declares that at runtime it's _exactly the same type_ as the corresponding input. So for example `SWAP` has a stack effect of `(a b -- b a)` which declares that the types in the output are the reverse of the input types. And `+` is declared `(a#$[] b#$[] -- a#$[])`, indicating that the parameters can be numbers, strings or arrays, and that the output type is the same as the first parameter's type.
