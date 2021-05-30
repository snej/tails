//
// core_words.hh
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
#include "word.hh"


namespace tails::core_words {

    /// All the words defined herein.
    extern const Word
        _INTERP, _INTERP2, _INTERP3, _INTERP4,
        _TAILINTERP, _RETURN, _LITERAL,
        DROP, DUP, OVER, ROT, SWAP, NOP,
        EQ, NE, EQ_ZERO, NE_ZERO,
        GE, GT, GT_ZERO,
        LE, LT, LT_ZERO,
        ABS, MAX, MIN,
        DIV, MOD, MINUS, MULT, PLUS,
        _BRANCH, _ZBRANCH,
        ONE, ZERO;
    
#ifndef SIMPLE_VALUE
    extern const Word NULL_, LENGTH, CALL, IFELSE;
#endif

    /// Array of pointers to the above core words, ending in nullptr
    extern const Word* const kWords[];

}
