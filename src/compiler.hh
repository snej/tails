//
// compiler.hh
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

#pragma once
#include "word.hh"
#include <optional>
#include <stdexcept>
#include <list>
#include <string>
#include <vector>


namespace tails {

    class Compiler;

    namespace core_words {
        extern const Word _LITERAL;
    }


    /// A subclass of Word that manages storage of its name and instructions, so it can be
    /// created at runtime.
    class CompiledWord : public Word {
    public:
        CompiledWord(std::string &&name, StackEffect effect, std::vector<Instruction> &&instrs);

        /// Constructs a word from a compiler. Call this instead of Compiler::finish.
        explicit CompiledWord(Compiler&);
    private:
        std::string const              _nameStr;   // Backing store for inherited _name
        std::vector<Instruction> const _instrs {}; // Backing store for inherited _instr
    };


    class compile_error : public std::runtime_error {
    public:
        compile_error(const char *msg, const char *loc) :runtime_error(msg), location(loc) { }
        compile_error(const std::string &msg, const char *loc) :runtime_error(msg), location(loc) { }

        const char *const location;
    };


    /// An object that assembles an interpreted word from a list of words to call.
    /// It computes and validates the word's stack effect.
    class Compiler {
    public:
        class EffectStack;

        /// A reference to a word and its parameter (if any), used during compilation.
        struct WordRef {
            WordRef(const Word &w)               :word(&w), param((Op)0) {assert(!w.hasAnyParam());}
            WordRef(const Word &w, Instruction p):word(&w), param(p) {assert(w.hasAnyParam());}
            WordRef(const Word &w, Value v)      :word(&w), param(v) {assert(w.hasValParam());}
            WordRef(const Word &w, intptr_t o)   :word(&w), param(o) {assert(w.hasIntParam());}

            WordRef(Value v)                     :WordRef(core_words::_LITERAL, v) { }
            WordRef(double d)                    :WordRef(core_words::_LITERAL, Value(d)) { }

            bool hasParam() const                {return word->hasAnyParam() || !word->isNative();}

            const Word*  word;                      // The word (interpreted or native)
            Instruction  param;                     // Optional parameter, if it has one

        private:
            friend class Compiler;
            WordRef() :param(intptr_t(0)) { }
        };


        struct SourceWord;
        /// An reference to a WordRef added to the Compiler.
        using InstructionPos = std::list<SourceWord>::iterator;


        Compiler();
        explicit Compiler(std::string name)         :Compiler() {_name = std::move(name);}
        ~Compiler();

        /// Declares what the word's stack effect must be.
        /// If the actual stack effect (computed during \ref finish) is different, a
        /// compile error is thrown.
        void setStackEffect(const StackEffect &f)   {assert(!_inputs); _effect = f;}

        /// Declares the maximum number of values that this word can read from the stack.
        /// The \ref finish method will detect if this is violated and throw an exception.
        /// (This is useful in a REPL when you're parsing input and know the current stack depth.)
        void setInputs(const StackEffectEntries &in) {assert(!_effect); _inputs = in;}

        void setInline()                            {_flags = Word::Flags(_flags | Word::Inline);}

        /// Breaks the input string into words and adds them.
        void parse(const std::string &input);

        //---- Adding individual words:


        /// Adds an instruction to a word being compiled.
        /// @return  An opaque reference to this instruction, that can be used later to fix branches.
        InstructionPos add(const WordRef&, const char *source =nullptr);

        /// Adds a word by inlining its definition, if it's interpreted. Native words added normally.
        void addInline(const Word&, const char *source);

        void addBranchBackTo(InstructionPos);

        /// Updates a previously-written `BRANCH` or `ZBRANCH` instruction, to branch to the
        /// next instruction to be written.
        /// @param src  The branch instruction to update.
        void fixBranch(InstructionPos src);

        /// Finishes a word being compiled. Adds a RETURN instruction, and registers it with the
        /// global Vocabulary (unless it's unnamed.)
        /// The Compiler object should not be used any more after this is called.
        CompiledWord finish();

        /// Creates a finished, anonymous CompiledWord from a list of word references.
        /// (Mostly just for tests.)
        static CompiledWord compile(std::initializer_list<WordRef> words);

    private:
        friend class CompiledWord;

        using BranchTarget = std::pair<char, InstructionPos>;

        std::vector<Instruction> generateInstructions();
        const char* parse(const char *input);
        Value parseString(std::string_view token);
        Value parseArray(const char* &input);
        Value parseQuote(const char* &input);
        void pushBranch(char identifier, const Word *branch =nullptr);
        InstructionPos popBranch(const char *matching);
        void computeEffect();
        void computeEffect(InstructionPos i,
                           EffectStack stack,
                           std::optional<StackEffect> &finalEffect);
        StackEffect effectOfIFELSE(InstructionPos);

        std::string                 _name;
        Word::Flags                 _flags {};
        std::list<SourceWord>       _words;
        std::optional<StackEffectEntries> _inputs;
        std::optional<StackEffect>  _effect;
        std::string_view            _curToken;
        std::vector<BranchTarget>   _controlStack;
    };


    /// Looks up the word for an instruction and returns it as a WordRef.
    /// If the word is _INTERP, the next word (at `instr[1]`) is returned instead.
    /// If the word has a parameter (like LITERAL or BRANCH), it's read from `instr[1]`.
    std::optional<Compiler::WordRef> DisassembleInstruction(const Instruction*);

    /// Same as \ref DisassembleInstruction, but also checks if this might be the parameter to a
    /// previous Instruction's word; in that case it returns the previous.
    std::optional<Compiler::WordRef> DisassembleInstructionOrParam(const Instruction*);

    /// Disassembles an entire interpreted word given its first instruction.
    std::vector<Compiler::WordRef> DisassembleWord(const Instruction *firstInstr);

}
