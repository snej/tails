//
// parser.cc
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

#include "parser.hh"
#include "vocabulary.hh"
#include "core_words.hh"
#include <optional>
#include <string>
#include <string_view>

using namespace std;


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


CompiledWord Parse(const char *input) {
    vector<Instruction> instrs;
    while (true) {
        string_view token = readToken(input);
        if (token.empty())
            break;
        if (const Word *word = gVocabulary.lookup(token); word) {
            if (!word->isNative())
                instrs.push_back(CALL._instr);
            instrs.push_back(word->_instr);
        } else if (auto ip = asNumber(token); ip) {
            instrs.push_back(LITERAL._instr);
            instrs.push_back(*ip);
        } else {
            throw runtime_error("Unknown word '" + string(token) + "'");
        }
    }
    instrs.push_back(RETURN._instr);
    return CompiledWord(move(instrs));
}
