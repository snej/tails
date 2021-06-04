//
// vocabulary.cc
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

#include "vocabulary.hh"
#include "core_words.hh"
#include "word.hh"
#include "utils.hh"
#include "gc.hh"


namespace tails {

    const Vocabulary Vocabulary::core(core_words::kWords);


    Vocabulary::Vocabulary(const Word* const *wordList) {
        add(wordList);
    }


    void Vocabulary::add(const Word* const *wordList) {
        while (*wordList)
            add(**wordList++);
    }


    void Vocabulary::add(const Word &word) {
        _words.insert({word.name(), &word});
    }


    const Word* Vocabulary::lookup(std::string_view name) const {
        if (auto i = _words.find(toupper(std::string(name))); i != _words.end())
            return i->second;
        else
            return nullptr;
    }


    const Word* Vocabulary::lookup(Instruction instr) const {
        for (auto i = _words.begin(); i != _words.end(); ++i) {
            if (i->second->instruction() == instr)
                return i->second;
        }
        return nullptr;
    }



    void VocabularyStack::push(const Vocabulary &v)  {
        _active.push_back(&v);
    }

    void VocabularyStack::pop() {
        assert(_active.size() > 1);
        _active.pop_back();
    }


    const Word* VocabularyStack::lookup(std::string_view name) const {
        for (auto vocab : _active)
            if (auto word = vocab->lookup(name); word)
                return word;
        return nullptr;
    }

    const Word* VocabularyStack::lookup(Instruction instr) const {
        for (auto vocab : _active)
            if (auto word = vocab->lookup(instr); word)
                return word;
        return nullptr;
    }

    VocabularyStack::iterator& VocabularyStack::iterator::operator++ () {
        if (++_iWord == _endWords) {
            if (++_iVoc != _endVoc) {
                _iWord = (*_iVoc)->begin();
                _endWords = (*_iVoc)->end();
            }
        }
        return *this;
    }


    void VocabularyStack::gcScan() {
#ifndef SIMPLE_VALUE
        for (auto word : *this)
            gc::object::scanWord(word);
#endif
    }


}
