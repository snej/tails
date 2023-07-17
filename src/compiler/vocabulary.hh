//
// vocabulary.hh
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
#include "instruction.hh"
#include <string_view>
#include <unordered_map>
#include <vector>
#include <assert.h>

namespace tails {
    class Word;

    /// A lookup table to find Words by name. Used by the Compiler.
    class Vocabulary {
    public:
        Vocabulary() = default;

        explicit Vocabulary(const Word* const *wordList);

        void add(const Word &word);

        void add(const Word* const *wordList);

        const Word* lookup(std::string_view name) const;
        const Word* lookup(Instruction const&) const;

        using map = std::unordered_map<std::string_view, const Word*>;
        using iterator = map::const_iterator;

        iterator begin() const    {return _words.begin();}
        iterator end() const      {return _words.end();}

        // The vocabulary of core words.
        static const Vocabulary core;

    private:
        map         _words;
    };


    /// A stack of (references to) Vocabulary objects to look up Words in. Used by the Compiler.
    class VocabularyStack {
    public:
        VocabularyStack()                           :_active{&Vocabulary::core} { }

        void push(const Vocabulary &v);
        void pop();

        /// Pushes v if it's not on the stack already. Returns true if it pushed it.
        bool use(const Vocabulary &v);

        const Word* lookup(std::string_view name) const;
        const Word* lookup(Instruction const&) const;

        /// The Vocabulary to which new Words are added, if any.
        Vocabulary* current() const                 {return _current;}

        void setCurrent(Vocabulary* v)              {_current = v;}
        void setCurrent(Vocabulary &v)              {return setCurrent(&v);}

        void gcScan();

        class iterator {
        public:
            const Word* operator* () const          {return _iWord->second;}
            const Word* operator-> () const         {return _iWord->second;}
            iterator& operator++ ();
            bool operator==(const iterator &other) const  {return _iVoc == other._iVoc;}
            bool operator!=(const iterator &other) const {return _iVoc != other._iVoc;}

        private:
            friend class VocabularyStack;
            iterator(std::vector<const Vocabulary*>::const_iterator beginVoc,
                     std::vector<const Vocabulary*>::const_iterator endVoc,
                     Vocabulary::iterator beginWords,
                     Vocabulary::iterator endWords)
            :_iVoc(beginVoc), _endVoc(endVoc), _iWord(beginWords), _endWords(endWords)
            { }

            std::vector<const Vocabulary*>::const_iterator  _iVoc, _endVoc;
            Vocabulary::iterator                            _iWord, _endWords;
        };

        iterator begin() const;
        iterator end() const;

    private:
        std::vector<const Vocabulary*>  _active;
        Vocabulary*                     _current = nullptr;
    };

}
