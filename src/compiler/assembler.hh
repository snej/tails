//
// assembler.hh
//
// 
//

#pragma once
#include "instruction.hh"

namespace tails {


    /** Helper class to generate bytecode. */
    class Assembler {
    public:
        Assembler() = default;

        int codeSize() const   {return int(_instrs.size());}

        void add(Word const& word, Instruction param = Instruction{nullptr}) {
            if (word.isNative()) {
                _instrs.push_back(word.instruction().opcode);
                if (word.parameters() > 0) {
                    assert(word.parameters() <= 1);
                    if (word.hasIntParams())
                        addParam(param.param.offset);
                    else if (word.hasValParams())
                        addParam(param.param.literal);
                    else if (word.hasWordParams())
                        addParam(param.param.word);
                    else
                        throw std::runtime_error("unknown param type");
                }
            } else {
                _instrs.push_back(Opcode::_INTERP);
                addParam(word.instruction().param.word);
            }
        }

        std::vector<Opcode> finish()&& {
            return std::move(_instrs);
        }

    private:
        template <typename T>
        void addParam(T param) {
            _instrs.insert(_instrs.end(), (Opcode*)&param, (Opcode*)(&param + 1));
        }

        std::vector<Opcode> _instrs;
    };
}
