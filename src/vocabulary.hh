//
// vocabulary.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include <string_view>
#include <unordered_map>

class Word;

/// A lookup table to find Words by name. Used by the Forth parser.
class Vocabulary {
public:
    Vocabulary();

    explicit Vocabulary(const Word* const *wordList);

    void add(const Word &word);

    const Word* lookup(std::string_view name);

    using map = std::unordered_map<std::string_view, const Word*>;
    using iterator = map::iterator;

    iterator begin()    {return _words.begin();}
    iterator end()      {return _words.end();}

private:
    map _words;
};


/// Global vocabulary. Every named Word adds itself to this.
extern Vocabulary gVocabulary;


