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
#include "compiler+stackcheck.hh"
#include "core_words.hh"
#include "vocabulary.hh"
#include <optional>
#include <sstream>
#include <string>
#include <string_view>


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


    CompiledWord::CompiledWord(Compiler &&compiler)
    :CompiledWord(move(compiler._name), {}, compiler.generateInstructions())
    {
        assert((compiler._flags & ~(Word::Inline | Word::Magic)) == 0);
        _flags = compiler._flags;
        _effect = compiler._effect;
    }


#pragma mark - COMPILER:


    Compiler::Compiler() {
        _words.push_back({NOP});
    }


    Compiler::~Compiler() = default;


    CompiledWord Compiler::compile(std::initializer_list<WordRef> words) {
        Compiler compiler;
        for (auto &ref : words)
            compiler.add(ref);
        return move(compiler).finish();
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


    CompiledWord Compiler::finish() && {
        return CompiledWord(move(*this)); // the CompiledWord constructor will call generateInstructions()
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
