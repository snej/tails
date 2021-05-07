//
// core_words.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "word.hh"


extern const Word
    CALL, RETURN, LITERAL,
    DROP, DUP, OVER, ROT, SWAP,
    EQ, NE, EQ_ZERO, NE_ZERO,
    GE, GT, GT_ZERO,
    LE, LT, LT_ZERO,
    ABS, MAX, MIN, SQUARE,
    DIV, MOD, MINUS, MULT, PLUS,
    BRANCH, ZBRANCH,
    ONE, ZERO;

/// Array of pointers to the above core words, ending in nullptr
extern const Word* const kCoreWords[];
