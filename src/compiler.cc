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


namespace tails {
    using namespace std;
    using namespace tails::core_words;


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
        _effect = *compiler._effect;
    }


    CompiledWord Compiler::compile(std::initializer_list<WordRef> words) {
        Compiler compiler;
        for (auto &ref : words)
            compiler.add(ref);
        return compiler.finish();
    }


    Compiler::InstructionPos Compiler::add(const WordRef &ref) {
        _words.push_back(ref);
        if (ref.hasParam()) {
            _words.push_back(NOP); // placeholder to keep indexes the same as in the compiled instrs
            _words.back().source = ref.source;
            return InstructionPos(_words.size() - 2);
        } else {
            return InstructionPos::None;
        }
    }


    const Compiler::WordRef& Compiler::operator[] (InstructionPos pos) {
        intptr_t i = intptr_t(pos);
        assert(i >= 0 && i < _words.size());
        return _words[i];
    }


    /// Adds a branch instruction (unless `branch` is NULL)
    /// and pushes its location onto the control-flow stack.
    void Compiler::pushBranch(char identifier, const Word *branch) {
        InstructionPos pos;
        if (branch)
            pos = add({*branch, intptr_t(-1)}, _curToken.data());
        else
            pos = nextInstructionPos();
        _controlStack.push_back({identifier, pos});
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


    void Compiler::fixBranch(InstructionPos src) {
        intptr_t srcPos = intptr_t(src), paramPos = srcPos + 1, dstPos = intptr_t(_words.size());
        assert(srcPos >= 0 && paramPos < dstPos);
        WordRef &branch = _words[srcPos];
        assert(branch.word == _ZBRANCH || branch.word == _BRANCH);
        assert(_words[paramPos].word == NOP);
        branch.param.offset = dstPos - paramPos - 1;
    }


    void Compiler::addBranchBackTo(InstructionPos pos) {
        intptr_t offset = intptr_t(pos) - (intptr_t(nextInstructionPos()) + 2);
        add({_BRANCH, offset}, _curToken.data());
    }


    vector<Instruction> Compiler::generateInstructions() {
        if (!_controlStack.empty())
            throw compile_error("Unfinished IF-ELSE-THEN or BEGIN-WHILE-REPEAT)", nullptr);

        // Add a RETURN:
        assert(_words.empty() || _words.back().word != _RETURN);
        add(_RETURN, (_words.empty() ? nullptr : _words.back().source));

        // Compute the stack effect:
        computeEffect();

        // If the word ends in a call to an interpreted word, we can make it a tail-call:
        WordRef *tailCallHere = nullptr;
        if (_words.size() >= 3) {
            // (The word would be before the RETURN and the NOP padding)
            auto lastWord = &_words[_words.size()-3];
            if (!lastWord->word.isNative())
                tailCallHere = lastWord;
        }

        // Assemble instructions:
        vector<Instruction> instrs;
        instrs.reserve(_words.size());
        for (WordRef &ref : _words) {
            if (ref.word != NOP) {
                if (!ref.word.isNative())
                    instrs.push_back((&ref == tailCallHere) ? _TAILINTERP : _INTERP);
                instrs.push_back(ref.word);
                if (ref.word.hasAnyParam())
                    instrs.push_back(ref.param);
            }
        }
        assert(instrs.size() == _words.size());
        return instrs;
    }


    CompiledWord Compiler::finish() {
        return CompiledWord(*this);
    }


    // Computes the stack effect of the word, throwing if it's inconsistent.
    void Compiler::computeEffect() {
        optional<StackEffect> effect;
        EffectVec instrEffects(_words.size());
        computeEffect(0, StackEffect(), instrEffects, effect);
        assert(effect);

        if (_effect && (effect->input() > _effect->input() ||
                        effect->output() != _effect->output())) {
            stringstream msg;
            msg << "Stack effect declared as (" << _effect->input() << " -- " << _effect->output()
                << "), but actual effect is (" << effect->input() << " -- " << effect->output() << ")";
            throw compile_error(msg.str(), nullptr);
        }
        *_effect = *effect;
    }


    // Subroutine that traces control flow, memoizing stack effects at each instruction.
    // @param i  The index in `_words` to start at
    // @param curEffect  The known stack effect before the word at `i`
    // @param instrEffects  Vector of known/memoized StackEffects at each instruction index
    // @param finalEffect  The cumulative stack effect will be stored here.
    // @throw compile_error if stack is inconsistent or there's an invalid branch offset.
    void Compiler::computeEffect(intptr_t i,
                                 StackEffect curEffect,
                                 EffectVec &instrEffects,
                                 optional<StackEffect> &finalEffect)
    {
        while (true) {
            // Look at the word at `i`:
            WordRef &cur = _words[i];
//            std::cout << "\t\tcomputeEffect at " << i << ", effect ("
//                    << curEffect.input() << "->" << curEffect.output() << ", max " << curEffect.max()
//                    << ") before " << cur.word._name << "\n";
            assert(cur.word != NOP);

            // Store (memoize) the current effect at i, or verify it matches a previously stored one:
            if (optional<StackEffect> &instrEffect = instrEffects[i]; instrEffect) {
                if (*instrEffect == curEffect)
                    return;
                else if (instrEffect->net() != curEffect.net())
                    throw compile_error("Inconsistent stack depth", cur.source);
                else
                    instrEffect = curEffect;
            } else {
                instrEffect = curEffect;
            }

            // apply the instruction's effect:
            curEffect = curEffect.then(cur.word.stackEffect());

            if (curEffect.input() > _maxInputs)
                throw compile_error("Stack would underflow", cur.source);

            if (cur.hasParam())
                ++i;

            if (cur.word == _RETURN) {
                // The current effect when RETURN is reached is the word's cumulative effect.
                // If there are multiple RETURNs, each must have the same effect.
                if (finalEffect && *finalEffect != curEffect)
                    throw compile_error("Inconsistent stack effects at RETURNs", cur.source);
                finalEffect = curEffect;
                return;

            } else if (cur.word == _BRANCH || cur.word == _ZBRANCH) {
                // Compute branch destination:
                auto dst = i + 1 + cur.param.offset;
                if (dst < 0 || dst >= _words.size() || _words[dst].word == NOP)
                    throw compile_error("Invalid BRANCH destination", cur.source);

                // If this is a 0BRANCH, recurse to follow the non-branch case too:
                if (cur.word == _ZBRANCH)
                    computeEffect(i + 1, curEffect, instrEffects, finalEffect);

                // Follow the branch:
                i = dst;

            } else {
                // Continue to next instruction:
                ++i;
            }
        }
    }


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
        else if (auto prev = DisassembleInstruction(instr - 1); prev && prev->word.hasAnyParam())
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
            if (ref->word == _BRANCH || ref->word == _ZBRANCH)
                maxJumpTo = max(maxJumpTo, i + 2 + instr[i+1].offset);
            else if (ref->word == _RETURN && i >= maxJumpTo)
                return instrs;
            if (ref->word.hasAnyParam())
                ++i;
        }
    }

}
