//
// compiler.cc
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

#include "compiler.hh"
#include "core_words.hh"
#include "vocabulary.hh"
#include <optional>
#include <sstream>
#include <string>
#include <string_view>


namespace tails {
    using namespace std;
    using namespace tails::core_words;


    CompiledWord::CompiledWord(string &&name, StackEffect effect, vector<Instruction> &&instrs)
    :_nameStr(move(name))
    ,_instrs(move(instrs))
    {
        _effect = effect;
        _instr = &_instrs.front();
        if (!_nameStr.empty()) {
            _name = _nameStr.c_str();
            Vocabulary::global.add(*this);
        }
    }


    CompiledWord Compiler::compile(std::initializer_list<WordRef> words) {
        Compiler compiler;
        for (auto &ref : words)
            compiler.add(ref);
        return compiler.finish();
    }


    Compiler::InstructionPos Compiler::add(const WordRef &ref) {
        _words.push_back(ref);
        if (ref.hasParam()) {
            _words.push_back(NOP); // placeholder to keep indexes the same as in the compiled instrs
            _words.back().source = ref.source;
            return InstructionPos(_words.size() - 2);
        } else {
            return InstructionPos::None;
        }
    }


    const Compiler::WordRef& Compiler::operator[] (InstructionPos pos) {
        intptr_t i = intptr_t(pos);
        assert(i >= 0 && i < _words.size());
        return _words[i];
    }


    /// Adds a branch instruction (unless `branch` is NULL)
    /// and pushes its location onto the control-flow stack.
    void Compiler::pushBranch(char identifier, const Word *branch) {
        InstructionPos pos;
        if (branch)
            pos = add({*branch, intptr_t(-1)}, _curToken.data());
        else
            pos = nextInstructionPos();
        _controlStack.push_back({identifier, pos});
    }

    /// Pops the control flow stack, checks that the popped identifier matches,
    /// and returns the address of its branch instruction.
    Compiler::InstructionPos Compiler::popBranch(const char *matching) {
        if (!_controlStack.empty()) {
            auto ctrl = _controlStack.back();
            if (strchr(matching, ctrl.first)) {
                _controlStack.pop_back();
                return ctrl.second;
            }
        }
        throw compile_error("no matching IF or WHILE", _curToken.data());
    }


    void Compiler::fixBranch(InstructionPos src) {
        intptr_t srcPos = intptr_t(src), paramPos = srcPos + 1, dstPos = intptr_t(_words.size());
        assert(srcPos >= 0 && paramPos < dstPos);
        WordRef &branch = _words[srcPos];
        assert(branch.word == _ZBRANCH || branch.word == _BRANCH);
        assert(_words[paramPos].word == NOP);
        branch.param.offset = dstPos - paramPos - 1;
    }


    void Compiler::addBranchBackTo(InstructionPos pos) {
        intptr_t offset = intptr_t(pos) - (intptr_t(nextInstructionPos()) + 2);
        add({_BRANCH, offset}, _curToken.data());
    }


    CompiledWord Compiler::finish() {
        if (!_controlStack.empty())
            throw compile_error("Unfinished IF-ELSE-THEN or BEGIN-WHILE-REPEAT)", nullptr);

        // Add a RETURN, if there's not one already:
        if (_words.empty() || _words.back().word != _RETURN) {
            const char *source = _words.empty() ? nullptr : _words.back().source;
            add(_RETURN, source);
        }

        // Compute the stack effect:
        computeEffect();

        // Assemble instructions:
        vector<Instruction> instrs;
        instrs.reserve(_words.size());
        for (WordRef &ref : _words) {
            if (ref.word != NOP) {
                if (!ref.word.isNative())
                    instrs.push_back(_INTERP);
                instrs.push_back(ref.word);
                if (ref.word.hasAnyParam())
                    instrs.push_back(ref.param);
            }
        }
        assert(instrs.size() == _words.size());

        return CompiledWord(move(_name), *_effect, move(instrs));
    }


    // Computes the stack effect of the word, throwing if it's inconsistent.
    void Compiler::computeEffect() {
        optional<StackEffect> effect;
        EffectVec instrEffects(_words.size());
        computeEffect(0, StackEffect(), instrEffects, effect);
        assert(effect);

        if (_effect && (effect->input() > _effect->input() ||
                        effect->output() != _effect->output())) {
            stringstream msg;
            msg << "Stack effect declared as (" << _effect->input() << " -- " << _effect->output()
                << "), but actual effect is (" << effect->input() << " -- " << effect->output() << ")";
            throw compile_error(msg.str(), nullptr);
        }
        *_effect = *effect;
    }


    // Subroutine that traces control flow, memoizing stack effects at each instruction.
    // @param i  The index in `_words` to start at
    // @param curEffect  The known stack effect before the word at `i`
    // @param instrEffects  Vector of known/memoized StackEffects at each instruction index
    // @param finalEffect  The cumulative stack effect will be stored here.
    // @throw compile_error if stack is inconsistent or there's an invalid branch offset.
    void Compiler::computeEffect(intptr_t i,
                                 StackEffect curEffect,
                                 EffectVec &instrEffects,
                                 optional<StackEffect> &finalEffect)
    {
        while (true) {
            // Look at the word at `i`:
            WordRef &cur = _words[i];
//            std::cout << "\t\tcomputeEffect at " << i << ", effect ("
//                    << curEffect.input() << "->" << curEffect.output() << ", max " << curEffect.max()
//                    << ") before " << cur.word._name << "\n";
            assert(cur.word != NOP);

            // Store (memoize) the current effect at i, or verify it matches a previously stored one:
            if (optional<StackEffect> &instrEffect = instrEffects[i]; instrEffect) {
                if (*instrEffect == curEffect)
                    return;
                else if (instrEffect->net() != curEffect.net())
                    throw compile_error("Inconsistent stack depth", cur.source);
                else
                    instrEffect = curEffect;
            } else {
                instrEffect = curEffect;
            }

            // apply the instruction's effect:
            curEffect = curEffect.then(cur.word.stackEffect());

            if (curEffect.input() > _maxInputs)
                throw compile_error("Stack would underflow", cur.source);

            if (cur.hasParam())
                ++i;

            if (cur.word == _RETURN) {
                // The current effect when RETURN is reached is the word's cumulative effect.
                // If there are multiple RETURNs, each must have the same effect.
                if (finalEffect && *finalEffect != curEffect)
                    throw compile_error("Inconsistent stack effects at RETURNs", cur.source);
                finalEffect = curEffect;
                return;

            } else if (cur.word == _BRANCH || cur.word == _ZBRANCH) {
                // Compute branch destination:
                auto dst = i + 1 + cur.param.offset;
                if (dst < 0 || dst >= _words.size() || _words[dst].word == NOP)
                    throw compile_error("Invalid BRANCH destination", cur.source);

                // If this is a 0BRANCH, recurse to follow the non-branch case too:
                if (cur.word == _ZBRANCH)
                    computeEffect(i + 1, curEffect, instrEffects, finalEffect);

                // Follow the branch:
                i = dst;

            } else {
                // Continue to next instruction:
                ++i;
            }
        }
    }


    #pragma mark - PARSER:


    /// Skips whitespace, then reads & returns the next token:
    /// * an empty string at EOF;
    /// * a string literal, starting and ending with double-quotes;
    /// * a "{" or "}";
    /// * a "[" or "]";
    /// * else the largest number of consecutive non-whitespace non-closing-brace characters.
    static string_view readToken(const char* &input) {
        // Skip whitespace
        while (*input != 0 && isspace(*input))
            ++input;

        // Read token
        auto start = input;
        switch (*input) {
            case 0:
                // EOF:
                break;
            case '"':
                // String literal:
                do {
                    ++input;
                    //TODO: Handle escape sequences
                } while (*input != 0 && *input != '"');
                if (*input)
                    ++input; // include the trailing quote
                break;
            case '{':
            case '[':
                // Open array or quotation -- just return the single delimiter character
                ++input;
                break;
            default:
                // General token: read until next whitespace or closing brace/bracket:
                do {
                    ++input;
                } while (*input != 0 && !isspace(*input) && *input != '}' && *input != ']');
                break;
        }
        return {start, size_t(input - start)};
    }


    /// Tries to parse `token` as an integer (decimal or hex) or floating-point number.
    /// Returns `nullopt` if it's not. Throws `compile_error` if it's an out-of-range number.
    static optional<double> asNumber(string_view token) {
        try {
            size_t pos;
            double d = stod(string(token), &pos);
            if (pos == token.size() && !isnan(d) && !isinf(d))
                return d;
        } catch (const std::out_of_range&) {
            throw compile_error("Number out of range", token.data());
        } catch (const std::invalid_argument&) {
            // if invalid number, just return nullopt
        }
        return nullopt;
    }


    void Compiler::parse(const char *input, bool allowMagic) {
        while (true) {
            string_view token = _curToken = readToken(input);
            const char *sourcePos = token.data();
            if (token.empty()) {
                // End of input
                break;

            } else if (token[0] == '"') {
                // String literal:
                add({LITERAL, parseString(token)}, sourcePos);

            } else if (token == "{") {
                add({LITERAL, parseArray(input)}, token.data());

            } else if (token == "IF") {
                // IF compiles into 0BRANCH, with offset TBD:
                pushBranch('i', &ZBRANCH);

            } else if (token == "ELSE") {
                // ELSE compiles into BRANCH, with offset TBD, and resolves the IF's branch:
                auto ifPos = popBranch("i");
                pushBranch('e', &BRANCH);
                fixBranch(ifPos);

            } else if (token == "THEN") {
                // THEN generates no code but completes the remaining branch from IF or ELSE:
                auto ifPos = popBranch("ie");
                fixBranch(ifPos);

            } else if (token == "BEGIN") {
                // BEGIN generates no code but remembers the current address:
                pushBranch('b');

            } else if (token == "WHILE") {
                // IF compiles into 0BRANCH, with offset TBD:
                if (_controlStack.empty() || _controlStack.back().first != 'b')
                    throw compile_error("no matching BEGIN for this WHILE", sourcePos);
                pushBranch('w', &ZBRANCH);

            } else if (token == "REPEAT") {
                // REPEAT generates a BRANCH back to the BEGIN's position,
                // and fixes up the WHILE to point to the next instruction:
                auto whilePos = popBranch("w");
                auto beginPos = popBranch("b");
                addBranchBackTo(beginPos);
                fixBranch(whilePos);

            } else if (const Word *word = Vocabulary::global.lookup(token); word) {
                // Known word is added as an instruction:
                if (!allowMagic && word->isMagic())
                        throw compile_error("Special word " + string(token)
                                            + " cannot be added by parser", sourcePos);
                if (word->hasAnyParam()) {
                    auto numTok = readToken(input);
                    auto param = asNumber(numTok);
                    if (!param || (*param != intptr_t(*param)))
                        throw compile_error("Invalid param after " + string(token), numTok.data());
                    if (word->hasIntParam())
                        add({*word, (intptr_t)*param}, sourcePos);
                    else
                        add({*word, Value(*param)}, sourcePos);
                } else {
                    add(*word, sourcePos);
                }

            } else if (auto np = asNumber(token); np) {
                // A number is added as a LITERAL instruction:
                add({LITERAL, Value(*np)}, sourcePos);

            } else {
                throw compile_error("Unknown word '" + string(token) + "'", sourcePos);
            }
        }
        _curToken = {};
    }


    Value Compiler::parseString(string_view token) {
#ifdef SIMPLE_VALUE
        throw compile_error("Strings not supported", token.data());
#else
        if (token.size() == 1 || token[token.size()-1] != '"')
            throw compile_error("Unfinished string literal", token.end());
        token = token.substr(1, token.size() - 2);
        return Value(token.data(), token.size());
#endif
    }


    Value Compiler::parseArray(const char* &input) {
#ifdef SIMPLE_VALUE
        throw compile_error("Arrays not supported", input);
#else
        Value arrayVal({});
        Value::Array *array = arrayVal.asArray();
        while (true) {
            string_view token = readToken(input);
            if (token == "}")
                break;
            else if (token.empty())
                throw compile_error("Unfinished array literal", input);
            else if (token[0] == '"')
                array->push_back(parseString(token));
            else if (token == "{")
                array->push_back(parseArray(input));
            else if (auto np = asNumber(token); np)
                array->push_back(Value(*np));
            else
                throw compile_error("Invalid literal '" + string(token) + "' in array", token.data());
        }
        return arrayVal;
#endif
    }


    #pragma mark - DISASSEMBLER:


    std::optional<Compiler::WordRef> DisassembleInstruction(const Instruction *instr) {
        const Word *word = Vocabulary::global.lookup(instr[0]);
        if (word && *word == _INTERP)
            word = Vocabulary::global.lookup(instr[1]);
        if (!word)
            return nullopt;
        else if (word->hasAnyParam())
            return Compiler::WordRef(*word, instr[1]);
        else
            return Compiler::WordRef(*word);
    }


    std::optional<Compiler::WordRef> DisassembleInstructionOrParam(const Instruction *instr) {
        if (auto word = DisassembleInstruction(instr); word)
            return word;
        else if (auto prev = DisassembleInstruction(instr - 1); prev && prev->word.hasAnyParam())
            return prev;
        else
            return nullopt;
    }


    vector<Compiler::WordRef> DisassembleWord(const Instruction *instr) {
        vector<Compiler::WordRef> instrs;
        intptr_t maxJumpTo = 0;
        for (int i = 0; ; i++) {
            auto ref = DisassembleInstruction(&instr[i]);
            if (!ref)
                throw runtime_error("Unknown instruction");
            instrs.push_back(*ref);
            if (ref->word == _BRANCH || ref->word == _ZBRANCH)
                maxJumpTo = max(maxJumpTo, i + 2 + instr[i+1].offset);
            else if (ref->word == _RETURN && i >= maxJumpTo)
                return instrs;
            if (ref->word.hasAnyParam())
                ++i;
        }
    }

}
