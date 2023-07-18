//
// signature_parser.hh
//
// 
//

#pragma once
#include "stack_effect.hh"

namespace tails {
    class Tokenizer;

    class SignatureParser {
    public:
        SignatureParser(Tokenizer& tokens) :_tokens(tokens) { }

        bool requireInputNames = true;

        /// Parses from the token stream. Assumes the opening `(` has already been read;
        /// consumes up through the matching `)`.
        void parse(std::string_view endingToken = ")");

        /// After parsing, the resulting StackEffect.
        StackEffect effect;
        
        /// After parsing, the names given to inputs/outputs, or empty strings if none.
        std::vector<std::string> inputNames, outputNames;

    private:
        void parseTypes(bool input, std::string const& name);

        Tokenizer& _tokens;
    };
    
}
