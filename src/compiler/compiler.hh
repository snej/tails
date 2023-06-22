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
    class EffectStack;
    class VocabularyStack;

    namespace core_words {
        extern const Word _LITERAL;
    }


    /// A subclass of Word that manages storage of its name and instructions, so it can be
    /// created at runtime.
    class CompiledWord : public Word {
    public:
        CompiledWord(std::string &&name, StackEffect effect, std::vector<Instruction> &&instrs);

        /// Constructs a word from a compiler. Call this instead of Compiler::finish.
        explicit CompiledWord(Compiler&&);

        /// Copies a CompiledWord, adding a name.
        CompiledWord(const CompiledWord&, std::string &&name);

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

        /// A reference to a word and its parameter (if any), used during compilation.
        struct WordRef {
            WordRef(const Word &w)               :word(&w), param((Op)0) {assert(!w.parameters());}
            WordRef(const Word &w, Instruction p):word(&w), param(p) {assert(w.parameters() > 0);}
            WordRef(const Word &w, Value v)      :word(&w), param(v) {assert(w.parameters() > 0);}
            WordRef(const Word &w, intptr_t o)   :word(&w), param(o) {assert(w.parameters() > 0);}

            WordRef(Value v)                     :WordRef(core_words::_LITERAL, v) { }
            WordRef(double d)                    :WordRef(core_words::_LITERAL, Value(d)) { }

            bool hasParam() const                {return word->parameters() || !word->isNative();}

            const Word*  word;                   // The word (interpreted or native)
            Instruction  param;                  // Optional parameter, if it has one

        private:
            friend class Compiler;
            WordRef() :param(intptr_t(0)) { }
        };

        Compiler();
        explicit Compiler(std::string name)         :Compiler() {_name = std::move(name);}
        ~Compiler();

        Compiler(const Compiler&) = delete;
        Compiler(Compiler&&);

        /// Declares what the word's stack effect must be.
        /// If the actual stack effect (computed during \ref finish) is different, a
        /// compile error is thrown.
        /// @param effect  The stack effect.
        /// @param canAddInputs  If true, additional inputs are allowed (the words can reach
        ///         deeper into the stack) and the effect will be updated accordingly.
        /// @param canAddOutputs  If true, additional outputs are allowed (more values may be left
        ///         on the stack) and the effect will be updated accordingly.

        void setStackEffect(const StackEffect &effect,
                            bool canAddInputs = false,
                            bool canAddOutputs = false)
        {
            _effect = effect;
            _effectCanAddInputs = canAddInputs;
            _effectCanAddOutputs = canAddOutputs;
        }

        /// Sets the word's input stack effect from the given actual stack. The output effect is TBD.
        void setInputStack(const Value *bottom, const Value *top);

        void setInline()                            {_flags = Word::Flags(_flags | Word::Inline);}

        /// Breaks the input string into words and adds them.
        void parse(const std::string &input);

        //---- Adding individual words:

        struct SourceWord;
        /// A reference to a WordRef added to the Compiler.
        using InstructionPos = std::list<SourceWord>::iterator;

        /// Adds an instruction to a word being compiled.
        /// @return  An opaque reference to this instruction, that can be used later to fix branches.
        InstructionPos add(const WordRef&, const char *source =nullptr);

        /// Adds a call to a compiled word.
        InstructionPos add(const Word* word,
                           const char *sourcePos =nullptr);
        
        /// Adds a call to a compiled word that takes a parameter.
        InstructionPos add(const Word* word,
                           intptr_t intParam,
                           const char *sourcePos =nullptr);

        InstructionPos addLiteral(Value v, const char *sourcePos =nullptr);

        /// Adds a word by inlining its definition, if it's interpreted. Native words added normally.
        InstructionPos addInline(const Word&, const char *source);

        /// Adds a BRANCH instruction that jumps back to the given position.
        void addBranchBackTo(InstructionPos);

        /// Adds a RECURSE instruction, a recursive call to the word being compiled.
        void addRecurse();

        /// Updates a previously-written `BRANCH` or `ZBRANCH` instruction, to branch to the
        /// next instruction to be written.
        /// @param src  The branch instruction to update.
        void fixBranch(InstructionPos src);

        //---- Creating the CompiledWord:

        /// Finishes a word being compiled. Adds a RETURN instruction, and registers it with the
        /// global Vocabulary (unless it's unnamed.)
        /// The Compiler object should not be used any more after this is called.
        CompiledWord finish() &&;

        /// Creates a finished, anonymous CompiledWord from a list of word references.
        /// (Mostly just for tests.)
        static CompiledWord compile(std::initializer_list<WordRef> words);

        //---- Vocabularies

        /// The vocabularies the parser looks up words from
        static VocabularyStack activeVocabularies;

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
        bool returnsImmediately(InstructionPos);
        void computeEffect();
        void computeEffect(InstructionPos i,
                           EffectStack stack);
        StackEffect effectOfIFELSE(InstructionPos, EffectStack&);

        std::string                 _name;
        Word::Flags                 _flags {};
        std::list<SourceWord>       _words;
        StackEffect                 _effect;
        bool                        _effectCanAddInputs = true;
        bool                        _effectCanAddOutputs = true;
        std::string_view            _curToken;
        std::vector<BranchTarget>   _controlStack;
    };

}
