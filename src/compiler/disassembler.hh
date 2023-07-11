//
// disassembler.hh
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
#include "compiler.hh"
#include "core_words.hh"
#include "word.hh"
#include "vocabulary.hh"

namespace tails {
    using namespace std;

    class Disassembler {
    public:
        Disassembler(const Instruction* pc) :_pc(pc) { }

        void setLiteral(bool literal)   {_literal = literal;}

        explicit operator bool() const  {return _pc != nullptr;}

        std::optional<Compiler::WordRef> _next() {
            std::optional<Compiler::WordRef> result;
            assert(_pc);
            const Word* word = _pc->nativeWord();
            if (!word)
                return result;
            if (!_literal && word->hasWordParams()) {
                // Interpreted word:
                assert(word->parameters() == 1); // TODO: Support _INTERP2 etc (?)
                word = Compiler::activeVocabularies.lookup(Instruction(_pc->param.word));
                if (word)
                    result = Compiler::WordRef(*word);
            } else if (word->parameters())
                result = Compiler::WordRef(*word, _pc->carefulCopy());
            else {
                if (word == &core_words::_RETURN)
                    _pc = nullptr;
                result = Compiler::WordRef(*word);
            }
            if (_pc)
                _pc = _pc->next();
            return result;
        }

        Compiler::WordRef next() {
            if (auto ref = _next(); ref)
                return *ref;
            throw runtime_error("Unknown instruction");
        }


        static Compiler::WordRef wordOrParamAt(const Instruction *instr) {
            Disassembler dis(instr);
            if (auto word = dis._next(); word)
                return *word;
            dis = Disassembler(instr - 1);
            if (auto prev = dis.next(); prev.word->parameters())
                return prev;
            else
                throw runtime_error("Unknown instruction");
        }


        static vector<Compiler::WordRef> disassembleWord(const Instruction *instr,
                                                         bool literal = false)
        {
            Disassembler dis(instr);
            dis.setLiteral(literal);
            vector<Compiler::WordRef> instrs;
            while (dis)
                instrs.push_back(dis.next());
            return instrs;
        }


    private:
        const Instruction* _pc;
        bool               _literal = false;
    };

}
