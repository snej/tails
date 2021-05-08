//
// compiler.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "word.hh"
#include <string>
#include <vector>

extern const Word LITERAL;


/// A Forth word definition compiled at runtime.
class CompiledWord : public Word {
public:
    struct WordRef {
        constexpr WordRef(const Word &w)            :word(w), param(0) {assert(!w.hasParam());}
        constexpr WordRef(const Word &w, int p)     :word(w), param(p) {assert(w.hasParam());}
        constexpr WordRef(int i)                    :WordRef(LITERAL, i) { }

        const Word& word;
        int         param;
    };

    /// Compiles Forth source code to an unnamed Word, but doesn't run it.
    static CompiledWord parse(const char *name = nullptr);

    /// Creates a finished CompiledWord from a list of word references.
    CompiledWord(const char *name, std::initializer_list<WordRef> words);

    /// Creates a finished, anonymous CompiledWord from a list of word references.
    CompiledWord(std::initializer_list<WordRef> words)  :CompiledWord(nullptr, words) { }

    //---- Incrementally building words:

    void declareEffect(StackEffect effect)  {_effect = effect;}

    /// Initializes a CompiledWord with a name (or none) but no instructions.
    /// \ref add and \ref finish need to be called before the word can be used.
    explicit CompiledWord(const char *name = nullptr);

    /// Adds an instruction to a word being compiled.
    void add(const WordRef&);

    /// Finishes a word being compiled. Adds a RETURN instruction, and registers it with the
    /// global Vocabulary (unless it's unnamed.)
    void finish();

private:
    using WordVec = std::vector<WordRef>;
    StackEffect computeEffect(WordVec::iterator i);

    std::string                 _nameStr;       // Backing store for inherited _name
    std::vector<Instruction>    _instrs {};     // Instructions; backing store for inherited _instr

    std::unique_ptr<WordVec> _tempWords;
};
