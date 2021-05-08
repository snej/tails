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


StackEffect CompiledWord::computeEffect(WordVec::iterator i) {
    StackEffect effect(0, 0);
    while (true) {
        WordRef ref = *i;
        if (ref.word.hasParam())
            ++i;
        effect = effect.then(ref.word._effect);
        if (&ref.word == &RETURN) {
            return effect;
        } else if (&ref.word == &BRANCH || &ref.word == &ZBRANCH) {
            if (ref.param < 0)
                throw runtime_error("Backwards branches not supported yet");    //TODO: Track this
            auto dst = i + 1 + ref.param;
            if (dst < _tempWords->begin() || dst >= _tempWords->end())
                throw runtime_error("Invalid BRANCH destination");
            if (&ref.word == &BRANCH) {
                i += ref.param;
            } else {
                // Conditional branch: have to follow both paths and see if they match:
                StackEffect eff1 = computeEffect(i + 1);
                StackEffect eff2 = computeEffect(dst);
                if (eff1.net() != eff2.net())
                    throw runtime_error("Inconsistent stack depths after 0BRANCH");
                return effect.then( eff1.merge(eff2) );
            }
        }
        ++i;
    }
}


void CompiledWord::finish() {
    if (_instrs.empty() || _instrs.back().native != RETURN._instr.native)
        add(RETURN);
    _instr = &_instrs.front();

    auto effect = computeEffect(_tempWords->begin());
    _tempWords.reset();
    if (_effect) {
        if (effect != _effect)
            throw runtime_error("Actual stack effect differs from declaration");
    } else {
        _effect = effect;
    }
    if (_name)
        Vocabulary::global.add(*this);
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
