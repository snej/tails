//
// compiler.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "word.hh"
#include <optional>
#include <string>
#include <vector>


namespace tails {

    namespace core_words {
        extern const Word LITERAL;
    }


    /// A Forth word definition compiled at runtime.
    class CompiledWord : public Word {
    public:
        struct WordRef {
            constexpr WordRef(const Word &w)            :word(w), param(0) {assert(!w.hasParam());}
            constexpr WordRef(const Word &w, int p)     :word(w), param(p) {assert(w.hasParam());}
            constexpr WordRef(int i)                    :WordRef(core_words::LITERAL, i) { }

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

        /// Initializes a CompiledWord with a name (or none) but no instructions.
        /// \ref add and \ref finish need to be called before the word can be used.
        explicit CompiledWord(const char *name = nullptr);

        /// An opaque reference to an instruction written to a CompiledWord in progress.
        enum class InstructionPos : int { None = 0 };

        /// Adds an instruction to a word being compiled.
        /// @return  An opaque reference to this instruction, that can be used later to fix branches.
        InstructionPos add(const WordRef&);

        /// Returns the word at the given position.
        const WordRef& operator[] (InstructionPos);

        /// Returns an opaque reference to the _next_ instruction to be added,
        InstructionPos nextInstructionPos() const       {return InstructionPos(_instrs.size() + 1);}

        /// Updates the branch target of a previously-written `BRANCH` or `ZBRANCH` instruction.
        /// @param src  The branch instruction to update.
        /// @param dst  The instruction it should jump to.
        void fixBranch(InstructionPos src, InstructionPos dst);

        /// Finishes a word being compiled. Adds a RETURN instruction, and registers it with the
        /// global Vocabulary (unless it's unnamed.)
        void finish();

    private:
        using WordVec = std::vector<WordRef>;
        using EffectVec = std::vector<std::optional<StackEffect>>;

        void computeEffect();
        void computeEffect(int i,
                           StackEffect effect,
                           EffectVec &instrEffects,
                           std::optional<StackEffect> &finalEffect);

        std::string                 _nameStr;       // Backing store for inherited _name
        std::vector<Instruction>    _instrs {};     // Instructions; backing store for inherited _instr
        std::unique_ptr<WordVec>    _tempWords;     // used only during building, until `finish`
    };


    /// Looks up the word for an instruction and returns it as a WordRef.
    /// If the word is CALL, the next word (at `instr[1]`) is returned instead.
    /// If the word has a parameter (like LITERAL or BRANCH), it's read from `instr[1]`.
    std::optional<CompiledWord::WordRef> DisassembleInstruction(const Instruction *instr);

    /// Disassembles an entire interpreted word given its first instruction.
    std::vector<CompiledWord::WordRef> DisassembleWord(const Instruction *firstInstr);

}
