//
// more_words.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "word.hh"

namespace tails::word {

    void endLine();

    extern const ROMWord
        PRINT,  // `.` -- print top of stack to stdout
        SP,     // `SP.` -- print a space character
        NL,     // `NL.` -- print a newline
        NLQ;    // `NL?` -- print a newline only if there are characters on the current line

    extern const Word* const kWords[];
}
