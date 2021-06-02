//
// more_words.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "word.hh"

namespace tails::word {

    void endLine();

    extern const Word PRINT, SP, CRLF, CRLFQ;

    extern const Word* const kWords[];
}
