//
// signature_parser.cc
//
// 
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

#include "signature_parser.hh"
#include "tokenizer.hh"

namespace tails {
    using namespace std;

    template <typename T, typename U>
    static bool contains(std::vector<T> const& vec, U const& val) {
        return std::find(vec.begin(), vec.end(), val) != vec.end();
    }


    void SignatureParser::parse(string_view endingToken) {
        bool inputs = true;
        Token tok;
        while ((tok = _tokens.next()).literal != endingToken) {
            if (inputs && tok.literal == "--") {
                inputs = false;
            } else if (tok.type == Token::Identifier) {
                string name(tok.literal);
                if (contains(inputNames, name) || contains(outputNames, name))
                    throw compile_error("Duplicate parameter/result name");
                parseTypes(inputs, name);
            } else if (tok.type == Token::Operator) {
                if (inputs && requireInputNames)
                    throw compile_error("Input parameter needs a name");
                _tokens.backUp();
                parseTypes(inputs, "");
            } else {
                throw compile_error("Syntax error");
            }
        }
        std::reverse(inputNames.begin(), inputNames.end());
        std::reverse(outputNames.begin(), outputNames.end());
    }

    void SignatureParser::parseTypes(bool input, string const& name) {
        TypeSet types;
        Token typeTok;
        while ((typeTok = _tokens.next()).type == Token::Operator) {
            switch (typeTok.literal[0]) {
                case '?':   types |= Value::ANull; break;
                case '#':   types |= Value::ANumber; break;
                case '$':   types |= Value::AString; break;
                case '[':
                    types |= Value::AnArray;
                    if (_tokens.next().literal != "]")
                        throw compile_error("Invalid array type; expected ']'");
                    break;
                case '{':
                    types |= Value::AQuote;
                    if (_tokens.peek().literal == "}") {
                        _tokens.consumePeeked();
                    } else {
                        // Recursively parse the quote type's stack effect:
                        SignatureParser quoteParser(_tokens);
                        quoteParser.requireInputNames = false;
                        quoteParser.parse("}");
                        types.withQuoteEffect(quoteParser.effect.withUnknownMax());
                    }
                    break;
                default:
                    goto exit;
            }
        }
    exit:
        _tokens.backUp();
        if (types.empty())
            types = TypeSet::anyType();

        if (input) {
            effect.addInput(types);
            inputNames.push_back(name);
        } else {
            effect.addOutput(types);
            outputNames.push_back(name);
        }
    }

}
