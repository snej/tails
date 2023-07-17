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
        extern const ROMWord _LITERAL, _INT;
    }


    /// An object that assembles an interpreted word from a list of words to call.
    /// It computes and validates the word's stack effect.
    class Compiler {
    public:

        /// A reference to a word and its parameter (if any), used during compilation.
        struct WordRef {
            WordRef(const Word &w)               :word(&w), param(nullptr) {assert(!w.parameters());}
//            WordRef(const Word &w, Instruction p):word(&w), param(p) {assert(w.parameters() > 0);}
//            WordRef(const Word &w, Value v)      :word(&w), param(v) {assert(w.parameters() > 0);}
//            WordRef(const Word &w, intptr_t o)   :word(&w), param(o) {assert(w.parameters() > 0);}
            WordRef(double d)                    {init(d);}

            template <typename T>
            WordRef(const Word &w, T&& t)
            :word(&w)
            ,param(std::forward<T>(t))
            {assert(w.parameters() > 0);}

            WordRef(Value v) {
                if (v.isDouble())
                    init(v.asDouble());
                else
                    init(v);
            }

            bool hasParam() const                {return word->parameters() || !word->isNative();}

            const Word*  word;                   // The word (interpreted or native)
            Instruction  param {nullptr};        // Optional parameter, if it has one

        private:
            friend class Compiler;
            WordRef() :param(intptr_t(0)) { }
            
            void init(double d) {
                if (canCastToInt16(d)) {
                    word = &core_words::_INT;
                    param = Instruction(int16_t(d));
                } else {
                    init(Value(d));
                }
            }
            void init(Value v) {
                word = &core_words::_LITERAL;
                param = Instruction(v);
            }
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
            _effectCanAddOutputTypes = canAddOutputs;
        }

        /// The word's stack effect (empty if it hasn't been set or computed yet.)
        StackEffect const& stackEffect() const      {return _effect;}

        /// Sets the word's input stack effect from the given actual stack. The output effect is TBD.
        void setInputStack(const Value *bottom, const Value *top);

        /// Makes the resulting Word inline.
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

        /// Adds an instruction to push a literal value on the stack.
        InstructionPos addLiteral(Value v, const char *sourcePos =nullptr);

        /// Adds an instruction to get a function arg and push it.
        /// @param stackOffset  Stack offset of the arg, at the time the function is called.
        ///                     0 = last (top) arg, -1 is previous, etc.
        InstructionPos addGetArg(int stackOffset, const char *sourcePos);

        InstructionPos addSetArg(int stackOffset, const char *sourcePos);

        void preservesArgs()                {_usesArgs = true;}

        /// Reserves stack space at the start of the function for another local variable.
        /// Returns the stack offset of the variable at the start of the function, >= 1.
        /// This is the `stackOffset` arg to pass to addGetArg/addSetArg to access that variable.
        int reserveLocalVariable();

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

        /// Extension of WordRef that adds private fields used by the compiler.
        struct SourceWord : public WordRef {
            SourceWord(const WordRef &ref, const char *source =nullptr);
            ~SourceWord();
            SourceWord(SourceWord&&);
            SourceWord& operator=(SourceWord&&);

            Opcode opcode() const {return word->instruction().opcode;}

            void branchesTo(InstructionPos pos) {
                branchTo = pos;
                pos->isBranchDestination = true;
            }

            // Returns true if is a RETURN, or a BRANCH to a RETURN.
            bool returnsImmediately() const {
                if (opcode() == Opcode::_BRANCH)
                    return (*branchTo)->returnsImmediately();
                else
                    return (opcode() == Opcode::_RETURN);
            }

            const char*  sourceCode;                        // Points to source code where word appears
            std::unique_ptr<EffectStack> knownStack;          // Stack effect at this point, once known
            std::optional<InstructionPos> branchTo;         // Points to where a branch goes
            int pc;                                         // Relative address during code-gen
            bool isBranchDestination = false;               // True if a branch points here
        };


    private:
        friend class CompiledWord;

        using BranchTarget = std::pair<char, InstructionPos>;

        std::vector<Opcode> generateInstructions();
        const char* parse(const char *input);
        Value parseString(std::string_view token);
        Value parseArray(const char* &input);
        Value parseQuote(const char* &input);
        void pushBranch(char identifier, const Word *branch =nullptr);
        InstructionPos popBranch(const char *matching);
        void computeEffect();
        void computeEffect(InstructionPos i,
                           EffectStack stack);

        std::string                 _name;
        Word::Flags                 _flags {};
        std::list<SourceWord>       _words;
        StackEffect          _effect;
        bool                        _effectCanAddInputs = true;
        bool                        _effectCanAddOutputs = true;
        bool                        _effectCanAddOutputTypes = true;
        std::string_view            _curToken;
        std::vector<BranchTarget>   _controlStack;
        int                         _nLocals = 0;
        bool                        _usesArgs = false;
    };

}
