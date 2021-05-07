//
// word.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "instruction.hh"


//======================== DEFINING WORDS ========================//


struct WordRef;


/// A Forth word definition: name, flags and code.
struct Word {
    enum Flags {
        None = 0,
        HasIntParam = 1 ///< This word is followed by an int parameter (LITERAL, BRANCH, 0BRANCH)
    };

    Word(const char *name, Op native, Flags flags =None);
    Word(const char *name, std::initializer_list<WordRef> words);
    Word(std::initializer_list<WordRef> words)  :Word(nullptr, words) { }
    Word(std::vector<Instruction>&&);

    const char*                    _name {};    // Forth name, or NULL if anonymous
    Op                             _native {};  // Native function pointer or NULL
    std::vector<Instruction>       _instrs {};  // Interpreted instructions (if not native)
    Flags                          _flags {};
};


/// A reference to a Word and optional following parameter;
/// used only temporarily, in the initializer_list of the Word constructor.
/// This is just a convenience for hand-assembling words, not a real part of the system.
struct WordRef {
    WordRef(const Word &word);
    WordRef(const Word &word, int param);
    WordRef(int i);

    Instruction _instrs[2];
    int8_t      _count = 2;
};


