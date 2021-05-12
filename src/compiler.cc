//
// compiler.cc
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
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
#include <string>
#include <string_view>


namespace tails {
    using namespace std;
    using namespace tails::core_words;


    /// Constructor for an interpreted word.
    CompiledWord::CompiledWord(const char *name)
    :_tempWords(new WordVec())
    {
        if (name) {
            _nameStr = name;
            _name = _nameStr.c_str();
        }
    }


    CompiledWord::CompiledWord(const char *name, std::initializer_list<WordRef> words)
    :CompiledWord(name)
    {
        size_t count = 1;
        for (auto &ref : words)
            count += 1 + ref.word.hasParam();
        _instrs.reserve(count);
        for (auto &ref : words)
            add(ref);
        finish();
    }


    CompiledWord::InstructionPos CompiledWord::add(const WordRef &ref) {
        if (!ref.word.isNative())
            _instrs.push_back(CALL);
        _instrs.push_back(ref.word._instr);
        _tempWords->push_back(ref);
        if (ref.word.hasParam()) {
            _instrs.push_back(ref.param);
            _tempWords->push_back(NOP); // placeholder to keep indexes the same as in _instrs
            return InstructionPos(_instrs.size() - 1);
        } else {
            return InstructionPos::None;
        }
    }


    const CompiledWord::WordRef& CompiledWord::operator[] (InstructionPos pos) {
        int i = int(pos) - 1;
        assert(_tempWords && i >= 0 && i < _tempWords->size());
        return (*_tempWords)[i];
    }


    void CompiledWord::fixBranch(InstructionPos src, InstructionPos dst) {
        int srcPos = int(src) - 1, paramPos = srcPos + 1, dstPos = int(dst) - 1;
        assert(srcPos >= 0 && paramPos < _instrs.size());
        assert(dstPos > srcPos && dstPos <= _instrs.size());
        auto &branch = (*_tempWords)[srcPos];
        assert(branch.word == ZBRANCH || branch.word == BRANCH);
        assert((*_tempWords)[paramPos].word == NOP);
        branch.param = (dstPos - paramPos - 1);
        _instrs[paramPos].param = branch.param;
    }


    void CompiledWord::finish() {
        add(RETURN);
        computeEffect();
        _instr = &_instrs.front();
        if (_name)
            Vocabulary::global.add(*this);
    }


    // Computes the stack effect of the word, throwing if it's inconsistent.
    void CompiledWord::computeEffect() {
        assert(_tempWords);
        optional<StackEffect> effect;
        EffectVec instrEffects(_tempWords->size());
        computeEffect(0, StackEffect(), instrEffects, effect);
        assert(effect);
        _effect = *effect;
        _tempWords.reset();
    }


    // Subroutine that traces control flow, memoizing stack effects at each instruction.
    // @param i  The index in `_tempWords` to start at
    // @param curEffect  The known stack effect before the word at `i`
    // @param instrEffects  Vector of known/memoized StackEffects at each instruction index
    // @param finalEffect  The cumulative stack effect will be stored here.
    // @throw runtime_error if stack is inconsistent or there's an invalid branch offset.
    void CompiledWord::computeEffect(int i,
                                     StackEffect curEffect,
                                     EffectVec &instrEffects,
                                     optional<StackEffect> &finalEffect)
    {
        while (true) {
            // Look at the word at `i`:
            WordRef &cur = (*_tempWords)[i];
//            std::cout << "\t\tcomputeEffect at " << i << ", effect ("
//                    << curEffect.input() << "->" << curEffect.output() << ", max " << curEffect.max()
//                    << ") before " << cur.word._name << "\n";
            assert(cur.word != NOP);

            // Store (memoize) the current effect at i, or verify it matches a previously stored one:
            if (optional<StackEffect> &instrEffect = instrEffects[i]; instrEffect) {
                if (*instrEffect == curEffect)
                    return;
                else if (instrEffect->net() != curEffect.net())
                    throw runtime_error("Inconsistent stack depth");
                else
                    instrEffect = curEffect;
            } else {
                instrEffect = curEffect;
            }

            // apply the instruction's effect:
            curEffect = curEffect.then(cur.word._effect);

            if (cur.word.hasParam())
                ++i;

            if (cur.word == RETURN) {
                // The current effect when RETURN is reached is the word's cumulative effect.
                // If there are multiple RETURNs, each must have the same effect.
                if (finalEffect && *finalEffect != curEffect)
                    throw runtime_error("Inconsistent stack effects at RETURNs");
                finalEffect = curEffect;
                return;

            } else if (cur.word == BRANCH || cur.word == ZBRANCH) {
                // Compute branch destination:
                auto dst = i + 1 + cur.param;
                if (dst < 0 || dst >= _tempWords->size() || (*_tempWords)[dst].word == NOP)
                    throw runtime_error("Invalid BRANCH destination");

                // If this is a 0BRANCH, recurse to follow the non-branch case too:
                if (cur.word == ZBRANCH)
                    computeEffect(i + 1, curEffect, instrEffects, finalEffect);

                // Follow the branch:
                i = dst;

            } else {
                // Continue to next instruction:
                ++i;
            }
        }
    }


    #pragma mark - PARSER:


    static string_view readToken(const char* &input) {
        // Skip whitespace
        while (*input != 0 && isspace(*input))
            ++input;
        // Read token
        auto start = input;
        while (*input != 0 && !isspace(*input))
            ++input;
        return {start, size_t(input - start)};
    }


    static optional<int> asNumber(string_view token) {
        try {
            size_t pos;
            int i = stoi(string(token), &pos, 0);
            if (pos == token.size())
                return i;
        } catch (exception &x) {
        }
        return nullopt;
    }


    CompiledWord CompiledWord::parse(const char *input) {
        vector<InstructionPos> ifStack;
        CompiledWord parsedWord(nullptr);
        while (true) {
            string_view token = readToken(input);
            if (token.empty())
                break;
            if (token == "IF") {
                // IF compiles into 0BRANCH, with offset TBD:
                ifStack.push_back(parsedWord.add({ZBRANCH, -1}));
            } else if (token == "ELSE") {
                // ELSE compiles into BRANCH, with offset TBD, and resolves the IF's branch:
                if (ifStack.empty())
                    throw runtime_error("ELSE without a matching IF");
                InstructionPos ifPos = ifStack.back();
                if (parsedWord[ifPos].word != ZBRANCH)
                    throw runtime_error("ELSE following ELSE");
                InstructionPos elsePos = parsedWord.add({BRANCH, -1});
                parsedWord.fixBranch(ifStack.back(), parsedWord.nextInstructionPos());
                ifStack.back() = elsePos;
            } else if (token == "THEN") {
                // THEN generates no code but completes the remaining branch from IF or ELSE:
                if (ifStack.empty())
                    throw runtime_error("THEN without a matching IF");
                parsedWord.fixBranch(ifStack.back(), parsedWord.nextInstructionPos());
                ifStack.pop_back();
            } else if (const Word *word = Vocabulary::global.lookup(token); word) {
                // Known word is added as an instruction:
                if (word->hasParam()) {
                    auto param = asNumber(readToken(input));
                    if (!param)
                        throw runtime_error("Invalid numeric param after " + string(token));
                    parsedWord.add({*word, *param});
                } else {
                    parsedWord.add(*word);
                }
            } else if (auto ip = asNumber(token); ip) {
                // Number is added as a LITERAL instruction:
                parsedWord.add({LITERAL, *ip});
            } else {
                throw runtime_error("Unknown word '" + string(token) + "'");
            }
        }
        if (!ifStack.empty())
            throw runtime_error("Unfinished IF/ELSE (missing THEN)");
        parsedWord.finish();
        return parsedWord;
    }


    #pragma mark - DISASSEMBLER:


    std::optional<CompiledWord::WordRef> DisassembleInstruction(const Instruction *instr) {
        const Word *word = Vocabulary::global.lookup(instr[0]);
        if (word && *word == CALL)
            word = Vocabulary::global.lookup(instr[1]);
        if (!word)
            return nullopt;
        else if (word->hasParam())
            return CompiledWord::WordRef(*word, instr[1].param);
        else
            return CompiledWord::WordRef(*word);
    }


    vector<CompiledWord::WordRef> DisassembleWord(const Instruction *instr) {
        vector<CompiledWord::WordRef> instrs;
        int maxJumpTo = 0;
        for (int i = 0; ; i++) {
            auto ref = DisassembleInstruction(&instr[i]);
            if (!ref)
                throw runtime_error("Unknown instruction");
            instrs.push_back(*ref);
            if (ref->word == BRANCH || ref->word == ZBRANCH)
                maxJumpTo = max(maxJumpTo, i + 2 + instr[i+1].param);
            else if (ref->word == RETURN && i >= maxJumpTo)
                return instrs;
            if (ref->word.hasParam())
                ++i;
        }
    }

}
