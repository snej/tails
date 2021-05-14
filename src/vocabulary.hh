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

namespace tails {
    class Word;

    /// A lookup table to find Words by name. Used by the Forth parser.
    class Vocabulary {
    public:
        Vocabulary();

        explicit Vocabulary(const Word* const *wordList);

        void add(const Word &word);

        const Word* lookup(std::string_view name);

        const Word* lookup(Instruction);

        using map = std::unordered_map<std::string_view, const Word*>;
        using iterator = map::iterator;

        iterator begin()    {return _words.begin();}
        iterator end()      {return _words.end();}

        static Vocabulary global;

    private:
        map _words;
    };

}
