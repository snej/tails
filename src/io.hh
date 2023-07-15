//
// io.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "stack_effect.hh"
#include "disassembler.hh"
#include "effect_stack.hh"
#include <iostream>
#include <sstream>

namespace tails {

    inline std::ostream& operator<< (std::ostream &out, const StackEffect &effect);
    
    // Prints a Value
    std::ostream& operator<< (std::ostream&, Value);  // defined in value.cc


    // Prints a TypeSet
    inline std::ostream& operator<< (std::ostream &out, TypeSet entry) {
        if (entry.canBeAnyType())
            out << "x";
        else if (!entry.exists())
            out << "∅";
        else {
            static constexpr const char *kNames[] = {"?", "#", "$", "[]", "{}"};
            for (int i = 0; i < Value::MaxType; ++i) {
                if (entry.canBeType(Value::Type(i))) {
                    out << kNames[i];
                }
            }
            if (entry.canBeType(Value::AQuote)) {
                out << '{';
                if (auto effect = entry.quoteEffect())
                    out << *effect;
                out << '}';
            }
        }
        if (entry.isInputMatch())
            out << "/" << entry.inputMatch();
        return out;
    }


    // Prints a TypesView
    inline std::ostream& operator<< (std::ostream &out, std::vector<TypeSet> const& types) {
        for (auto i = types.rbegin(); i != types.rend(); ++i) {
            if (i != types.rbegin()) out << ' ';
            out << *i;
        }
        return out;
    }


    // Prints a StackEffect
    inline std::ostream& operator<< (std::ostream &out, const StackEffect &effect) {
        return out << effect.inputs() << " -- " << effect.outputs();
    }


    static inline void disassemble(std::ostream& out, Compiler::WordRef const& wordRef) {
        out << (wordRef.word->name() ? wordRef.word->name() : "???");
        if (wordRef.word->parameters() > 0) {
            out << '<';
            if (wordRef.word == &core_words::_DROPARGS)
                out << int(wordRef.param.param.drop.locals) << ","
                    << int(wordRef.param.param.drop.results);
            else if (wordRef.word->hasIntParams())
                out << wordRef.param.param.offset;
            else if (wordRef.word->hasValParams())
                out << wordRef.param.param.literal;
            else if (wordRef.word->hasWordParams())
                out << Compiler::activeVocabularies.lookup(Instruction(wordRef.param.param.word))->name();
            out << '>';
        }
    }

    static inline void disassemble(std::ostream& out, const Word &word) {
        int n = 0;
        for (auto &wordRef : Disassembler::disassembleWord(word.instruction().param.word, true)) {
            if (n++) out << ' ';
            disassemble(out, wordRef);
        }
    }

    static inline std::string disassemble(const Compiler::WordRef &wordRef) {
        std::stringstream out;
        disassemble(out, wordRef);
        return out.str();
    }

    static inline std::string disassemble(const Word &word) {
        std::stringstream out;
        disassemble(out, word);
        return out.str();
    }

}
