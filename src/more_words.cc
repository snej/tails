//
// more_words.cc
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

#include "more_words.hh"
#include "io.hh"
#include "stack_effect_parser.hh"
#include <iostream>


namespace tails::word {
    using namespace std;


#pragma mark - I/O:

    static bool sAtLeftMargin = true;

    NATIVE_WORD(PRINT, ".", "a --"_sfx, 0) {
        std::cout << *(sp--);
        sAtLeftMargin = false;
        NEXT();
    }

    NATIVE_WORD(SP, "SP.", "--"_sfx, 0) {
        std::cout << ' ';
        sAtLeftMargin = false;
        NEXT();
    }

    NATIVE_WORD(NL, "NL.", "--"_sfx, 0) {
        std::cout << '\n';
        sAtLeftMargin = true;
        NEXT();
    }

    void endLine() {
        if (!sAtLeftMargin) {
            std::cout << '\n';
            sAtLeftMargin = true;
        }
    }

    NATIVE_WORD(NLQ, "NL?", "--"_sfx, 0) {
        endLine();
        NEXT();
    }



#pragma mark - LIST OF WORDS:


    // This null-terminated list is used to register these words in the Vocabulary at startup.

    const Word* const kWords[] = {
        &PRINT, &SP, &NL, &NLQ,
        nullptr
    };

}
