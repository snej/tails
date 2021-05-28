//
// compiler.cc
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

#include "compiler.hh"
#include "core_words.hh"
#include "vocabulary.hh"
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>


namespace tails {
    using namespace std;
    using namespace tails::core_words;


#pragma mark - COMPILEDWORD:


    CompiledWord::CompiledWord(string &&name, StackEffect effect, vector<Instruction> &&instrs)
    :_nameStr(move(name))
    ,_instrs(move(instrs))
    {
        _effect = effect;
        _instr = &_instrs.front();
        if (!_nameStr.empty()) {
            _name = _nameStr.c_str();
            Vocabulary::global.add(*this);
        }
    }


    CompiledWord::CompiledWord(Compiler &compiler)
    :CompiledWord(move(compiler._name), {}, compiler.generateInstructions())
    {
        assert((compiler._flags & ~(Word::Inline | Word::Magic)) == 0);
        _flags = compiler._flags;
        _effect = *compiler._effect;
    }


#pragma mark - EFFECTSTACK:


    static string format(const char *fmt, ...) {
        char *str = nullptr;
        va_list args;
        va_start(args, fmt);
        vasprintf(&str, fmt, args);
        va_end(args);
        string result(str);
        free(str);
        return result;
    }


    /// Simulates the runtime stack at compile time, while verifying stack effects.
    class Compiler::EffectStack {
    public:
        using Item = variant<StackEffectEntry,Value>;

        EffectStack() { }

        void initialize(const StackEffect &initial) {
            for (int i = 0; i < initial.inputs(); ++i)
                _stack.emplace_back(initial.input(i));
            _maxDepth = _initialDepth = depth();
        }

        void initialize(const StackEffectEntries &entries) {
            for (auto entry : entries)
                _stack.emplace_back(entry);
            _maxDepth = _initialDepth = depth();
        }

        size_t depth() const {return _stack.size();}

        const Item& at(size_t i) const {return _stack[_stack.size() - 1 - i];}

        bool operator==(const EffectStack &other) const {
            return _stack == other._stack;
        }

        /// Adds the stack effect of a word. Throws an exception on failure.
        void add(const StackEffect &effect, const Word *word, const char *sourceCode) {
            // Check that the inputs match what's on the stack:
            const auto nInputs = effect.inputs();
            if (nInputs > depth())
                throw compile_error(format("Stack underflow calling `%s`: %zu needed, %zu available",
                                           word->name(), nInputs, depth()),
                                    sourceCode);
            Item inputs[max(nInputs, 1)];
            for (int i = 0; i < nInputs; ++i) {
                auto input = effect.input(i);
                auto &item = at(i);
                inputs[i] = item;
                optional<Value::Type> badType;
                if (auto valP = std::get_if<Value>(&item); valP) {
                    if (!input.canBeType(valP->type())) {
                        badType = valP->type();
                    }
                } else {
                    badType = (std::get<StackEffectEntry>(item) - input).firstType();
                }
                if (badType)
                    throw compile_error(format("Type mismatch passing %s to `%s` at depth %zu",
                                               Value::typeName(*badType), word->name(), i),
                                        sourceCode);

            }

            _maxDepth = max(_maxDepth, depth() + effect.max());

            // Pop the inputs off the stack:
            _stack.resize(depth() - nInputs);

            // Push the outputs to the stack:
            for (int i = effect.outputs() - 1; i >= 0; --i) {
                StackEffectEntry ef = effect.output(i);
                if (auto in = ef.inputMatch(); in >= 0)
                    _stack.emplace_back(inputs[in]);
                else
                    _stack.emplace_back(ef);
            }
        }

        void add(Value value) {
            _stack.emplace_back(value);
            _maxDepth = max(_maxDepth, depth());
        }

        void mergeWith(const EffectStack &other, const char *sourceCode) {
            size_t d = depth();
            if (d != other.depth())
                throw compile_error("Inconsistent stack depth", sourceCode);
            for(size_t i = 0; i < d; ++i) {
                Item &mine = _stack[_stack.size() - 1 - i];
                const Item others = other.at(i);
                if (others != mine)
                    mine = itemEntry(mine) | itemEntry(others);
            }
        }

        void checkOutputs(const StackEffect &effect) const {
            const auto nOutputs = effect.outputs();
            if (nOutputs != depth())
                throw compile_error("Wrong number of outputs", nullptr);
            for (int i = 0; i < nOutputs; ++i) {
                auto ef = effect.output(i);
                auto &item = at(i);
                if (auto valP = std::get_if<Value>(&item); valP) {
                    if (!ef.canBeType(valP->type()))
                        throw compile_error("Output type mismatch", nullptr);
                } else {
                    if (std::get<StackEffectEntry>(item) > ef)
                        throw compile_error("Output type mismatch", nullptr);
                }
            }
        }

        StackEffect outputEffect() const {
            StackEffect effect;
            for (auto i = _stack.rbegin(); i != _stack.rend(); ++i)
                effect.addOutput(itemEntry(*i));
            return effect.withMax(int(_maxDepth - _initialDepth));
        }

    private:
        static StackEffectEntry itemEntry(const Item &item) {
            if (auto valP = std::get_if<Value>(&item); valP)
                return StackEffectEntry(valP->type());
            else
                return std::get<StackEffectEntry>(item);
        }

        std::vector<Item> _stack;
        size_t            _initialDepth = 0;
        size_t            _maxDepth = 0;
    };


#pragma mark - SOURCEWORD:


    /// Extension of WordRef that adds private fields used by the compiler.
    struct Compiler::SourceWord : public Compiler::WordRef {
        SourceWord(const WordRef &ref, const char *source =nullptr)
        :WordRef(ref)
        ,sourceCode(source)
        { }

        const char*  sourceCode;                        // Points to source code where word appears
        std::optional<EffectStack> knownStack;          // Stack effect at this point, once known
        std::optional<InstructionPos> branchDestination;// Points to where a branch goes
        int pc;                                         // Relative address during code-gen
    };


#pragma mark - COMPILER:


    Compiler::Compiler() {
        _words.push_back({NOP});
    }


    Compiler::~Compiler() = default;


    CompiledWord Compiler::compile(std::initializer_list<WordRef> words) {
        Compiler compiler;
        for (auto &ref : words)
            compiler.add(ref);
        return compiler.finish();
    }


    Compiler::InstructionPos Compiler::add(const WordRef &ref, const char *source) {
        _words.back() = SourceWord(ref, source);
        auto i = prev(_words.end());
        _words.push_back({NOP});
        return i;
    }


    void Compiler::addInline(const Word &word, const char *source) {
        if (word.isNative()) {
            add({word});
        } else {
            const Instruction *ip = word.instruction().word;    // first Instruction
            for (; *ip != _RETURN.instruction(); ++ip) {
                WordRef ref = DisassembleInstruction(ip).value();
                add(ref, source);
                if (ref.word->hasAnyParam())
                    ++ip;
            }
        }
    }


    void Compiler::addBranchBackTo(InstructionPos pos) {
        add({_BRANCH, intptr_t(-1)})->branchDestination = pos;
    }

    void Compiler::fixBranch(InstructionPos src) {
        src->branchDestination = prev(_words.end());
    }


    /// Adds a branch instruction (unless `branch` is NULL)
    /// and pushes its location onto the control-flow stack.
    void Compiler::pushBranch(char identifier, const Word *branch) {
        InstructionPos branchRef;
        if (branch)
            branchRef = add({*branch, intptr_t(-1)}, _curToken.data());
        else
            branchRef = prev(_words.end()); // Will point to next word to be added
        _controlStack.push_back({identifier, branchRef});
    }

    /// Pops the control flow stack, checks that the popped identifier matches,
    /// and returns the address of its branch instruction.
    Compiler::InstructionPos Compiler::popBranch(const char *matching) {
        if (!_controlStack.empty()) {
            auto ctrl = _controlStack.back();
            if (strchr(matching, ctrl.first)) {
                _controlStack.pop_back();
                return ctrl.second;
            }
        }
        throw compile_error("no matching IF or WHILE", _curToken.data());
    }


    vector<Instruction> Compiler::generateInstructions() {
        if (!_controlStack.empty())
            throw compile_error("Unfinished IF-ELSE-THEN or BEGIN-WHILE-REPEAT)", nullptr);

        // Add a RETURN, replacing the "next word" placeholder:
        assert(_words.back().word == &NOP);
        _words.back() = {_RETURN};

        // Compute the stack effect:
        computeEffect();

        // If the word ends in a call to an interpreted word, we can make it a tail-call:
        SourceWord *tailCallHere = nullptr;
        if (_words.size() >= 2) {
            SourceWord *lastWord = &*prev(prev(_words.end()));     // look before the RETURN
            if (!lastWord->word->isNative())
                tailCallHere = lastWord;
        }

        // Assign the relative pc of each word, leaving space for parameters:
        int pc = 0;
        for (SourceWord &ref : _words) {
            ref.pc = pc++;
            if (ref.hasParam())
                pc++;
        }

        // Assemble instructions:
        vector<Instruction> instrs;
        instrs.reserve(pc);
        for (SourceWord &ref : _words) {
            if (!ref.word->isNative())
                instrs.push_back((&ref == tailCallHere) ? _TAILINTERP : _INTERP);
            instrs.push_back(*ref.word);
            if (ref.branchDestination)
                ref.param.offset = (*ref.branchDestination)->pc - ref.pc - 2;
            if (ref.word->hasAnyParam())
                instrs.push_back(ref.param);
        }
        return instrs;
    }


    CompiledWord Compiler::finish() {
        return CompiledWord(*this); // the CompiledWord constructor will call generateInstructions()
    }


#pragma mark - STACK CHECKER:


    // Computes the stack effect of the word, throwing if it's inconsistent.
    void Compiler::computeEffect() {
        EffectStack stack;
        if (_effect)
            stack.initialize(*_effect);
        else if (_inputs)
            stack.initialize(*_inputs);

        optional<StackEffect> effect;
        computeEffect(_words.begin(), stack, effect);
        assert(effect);

        if (_effect && (effect->inputs() > _effect->inputs() ||
                        effect->outputs() != _effect->outputs())) {
            stringstream msg;
            msg << "Stack effect declared as (" << _effect->inputs() << " -- " << _effect->outputs()
                << "), but actual effect is (" << effect->inputs() << " -- " << effect->outputs() << ")";
            throw compile_error(msg.str(), nullptr);
        }
        *_effect = *effect;
    }


    // Subroutine that traces control flow, memoizing stack effects at each instruction.
    // @param i  The item in `_words` to start at
    // @param curStack  The known stack before the word at `i`
    // @param finalEffect  The cumulative stack effect will be stored here.
    // @throw compile_error if stack is inconsistent or there's an invalid branch offset.
    void Compiler::computeEffect(InstructionPos i,
                                 EffectStack curStack,
                                 optional<StackEffect> &finalEffect)
    {
        while (true) {
            assert(i != _words.end());
            // Store (memoize) the current stack at i, or verify it matches a previously stored one:
            if (i->knownStack) {
                if (*i->knownStack == curStack)
                    return;
                else
                    curStack.mergeWith(*i->knownStack, i->sourceCode);
            }
            i->knownStack = curStack;

            // determine the instruction's effect:
            if (i->word == &_LITERAL) {
                curStack.add(i->param.literal);
            } else {
                StackEffect nextEffect = i->word->stackEffect();
                if (nextEffect.isWeird()) {
    #ifndef SIMPLE_VALUE
                    if (i->word == &IFELSE)
                        nextEffect = effectOfIFELSE(i);
                    else
    #endif
                        throw compile_error("Oops, don't know word's stack effect", i->sourceCode);
                }

                // apply the instruction's effect:
                curStack.add(nextEffect, i->word, i->sourceCode);
            }

            if (i->word == &_RETURN) {
                // The current effect when RETURN is reached is the word's cumulative effect.
                // If there are multiple RETURNs, each must have the same effect.
                if (finalEffect)
                    curStack.checkOutputs(*finalEffect);
                else
                    finalEffect = curStack.outputEffect();
                return;

            } else if (i->word == &_BRANCH || i->word == &_ZBRANCH) {
                assert(i->branchDestination);
                // If this is a 0BRANCH, recurse to follow the non-branch case too:
                if (i->word == &_ZBRANCH)
                    computeEffect(next(i), curStack, finalEffect);

                // Follow the branch:
                i = *i->branchDestination;

            } else {
                // Continue to next instruction:
                ++i;
            }
        }
    }


#ifndef SIMPLE_VALUE
    __unused static const Word* quoteAt(Compiler::InstructionPos i) {
        return (i->word == &_LITERAL) ? i->param.literal.asQuote() : nullptr;
    }

    StackEffect Compiler::effectOfIFELSE(InstructionPos i) {
#if 0 //TEMP
        // Special case for IFELSE, which has a non-constant stack effect.
        // The two prior words must be LITERALs that push quotations:
        if (i != _words.begin()) {
            if (auto ii = prev(i); ii != _words.begin()) {
                const Word *lit1 = quoteAt(ii);
                const Word *lit2 = quoteAt(prev(ii));
                if (lit1->stackEffect().net() == lit2->stackEffect().net())
                    return lit1->stackEffect().either(lit2->stackEffect());
                throw compile_error("inconsistent stack effects for IFELSE", i->sourceCode);
            }
        }
#endif
        throw compile_error("IFELSE must be preceded by two quotations", i->sourceCode);
    }
#endif
    

    #pragma mark - DISASSEMBLER:


    std::optional<Compiler::WordRef> DisassembleInstruction(const Instruction *instr) {
        const Word *word = Vocabulary::global.lookup(instr[0]);
        if (word && (*word == _INTERP || *word == _TAILINTERP))
            word = Vocabulary::global.lookup(instr[1]);
        if (!word)
            return nullopt;
        else if (word->hasAnyParam())
            return Compiler::WordRef(*word, instr[1]);
        else
            return Compiler::WordRef(*word);
    }


    std::optional<Compiler::WordRef> DisassembleInstructionOrParam(const Instruction *instr) {
        if (auto word = DisassembleInstruction(instr); word)
            return word;
        else if (auto prev = DisassembleInstruction(instr - 1); prev && prev->word->hasAnyParam())
            return prev;
        else
            return nullopt;
    }


    vector<Compiler::WordRef> DisassembleWord(const Instruction *instr) {
        vector<Compiler::WordRef> instrs;
        intptr_t maxJumpTo = 0;
        for (int i = 0; ; i++) {
            auto ref = DisassembleInstruction(&instr[i]);
            if (!ref)
                throw runtime_error("Unknown instruction");
            instrs.push_back(*ref);
            if (ref->word == &_BRANCH || ref->word == &_ZBRANCH)
                maxJumpTo = max(maxJumpTo, i + 2 + instr[i+1].offset);
            else if (ref->word == &_RETURN && i >= maxJumpTo)
                return instrs;
            if (ref->hasParam())
                ++i;
        }
    }

}
