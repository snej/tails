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

using namespace std;


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


void CompiledWord::add(const WordRef &ref) {
    if (!ref.word.isNative())
        _instrs.push_back(CALL);
    _instrs.push_back(ref.word._instr);
    if (ref.word.hasParam())
        _instrs.push_back(ref.param);

    _tempWords->push_back(ref);
    if (ref.word.hasParam())
        _tempWords->push_back(NOP); // placeholder to keep indexes the same as in _instrs
}


void CompiledWord::declareEffect(StackEffect effect) {
    if (_effect && effect != _effect)
        throw runtime_error("Actual stack effect differs from declaration");
    _effect = effect;
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
    StackEffect effect;
    computeEffect(0, StackEffect(), effect);
    _tempWords.reset();
    declareEffect(effect);
}


// Subroutine that traces control flow, memoizing stack effects at each instruction.
// @param i  The index in `_tempWords` to start at
// @param curEffect  The known stack effect before the word at `i`
// @param finalEffect  The cumulative stack effect will be stored here.
void CompiledWord::computeEffect(int i, StackEffect curEffect, StackEffect &finalEffect) {
    while (true) {
        // Look at the word at `i`:
        WordRef &cur = (*_tempWords)[i];
        //std::cout << "\t\tcomputeEffect at " << i << ", effect (" << curEffect.input() << "," << curEffect.output() << ") before " << cur.word._name << "\n";
        if (cur.word.hasParam())
            ++i;
        assert(cur.word != NOP);
        if (cur.effectNow) {
            // If we've been at this instruction before (due to a back BRANCH),
            // we can stop, but the current effect must match the previous one:
            if (cur.effectNow != curEffect)
                throw runtime_error("Inconsistent stack depth");
            return;
        }
        
        // Remember current effect, then apply the instruction's effect:
        cur.effectNow = curEffect;
        curEffect = curEffect.then(cur.word._effect);

        if (cur.word == RETURN) {
            // The current effect when RETURN is reached is the word's cumulative effect.
            // If there are multiple RETURNs, each must have the same effect.
            if (finalEffect && finalEffect != curEffect)
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
                computeEffect(i + 1, curEffect, finalEffect);

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
    CompiledWord parsedWord(nullptr);
    while (true) {
        string_view token = readToken(input);
        if (token.empty())
            break;
        if (const Word *word = Vocabulary::global.lookup(token); word) {
            if (word->hasParam()) {
                auto param = asNumber(readToken(input));
                if (!param)
                    throw runtime_error("Invalid numeric param after " + string(token));
                parsedWord.add({*word, *param});
            } else {
                parsedWord.add(*word);
            }
        } else if (auto ip = asNumber(token); ip) {
            parsedWord.add(*ip);
        } else {
            throw runtime_error("Unknown word '" + string(token) + "'");
        }
    }
    parsedWord.finish();
    return parsedWord;
}
