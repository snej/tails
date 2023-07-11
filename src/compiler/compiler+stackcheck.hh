//
// compiler+stackcheck.cc
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
#include "core_words.hh"
#include "effect_stack.hh"
#include "core_words.hh"
#include "utils.hh"
#include <optional>

namespace tails {
    using namespace std;
    using namespace tails::core_words;


#pragma mark - SOURCEWORD:


    /// Extension of WordRef that adds private fields used by the compiler.
    struct Compiler::SourceWord : public Compiler::WordRef {
        SourceWord(const WordRef &ref, const char *source =nullptr)
        :WordRef(ref)
        ,sourceCode(source)
        { }

        void branchesTo(InstructionPos pos) {
            branchTo = pos;
            pos->isBranchDestination = true;
        }

        const char*  sourceCode;                        // Points to source code where word appears
        std::optional<EffectStack> knownStack;          // Stack effect at this point, once known
        std::optional<InstructionPos> branchTo;         // Points to where a branch goes
        int pc;                                         // Relative address during code-gen
        const Word* interpWord = nullptr;               // Which INTERP-family word to use
        bool isBranchDestination = false;               // True if a branch points here
    };


#pragma mark - COMPILER STACK CHECKER:


    // Computes the stack effect of the word, throwing if it's inconsistent.
    void Compiler::computeEffect() {
        computeEffect(_words.begin(), EffectStack(_effect));
    }


    // Subroutine that traces control flow, memoizing stack effects at each instruction.
    // @param i  The item in `_words` to start at
    // @param curStack  The known stack before the word at `i`
    // @param finalEffect  The cumulative stack effect will be stored here.
    // @throw compile_error if stack is inconsistent or there's an invalid branch offset.
    void Compiler::computeEffect(InstructionPos i, EffectStack curStack)
    {
        while (true) {
            assert(i != _words.end());
            // Store (memoize) the current stack at i, or verify it matches a previously stored one:
            if (i->knownStack) {
                if (*i->knownStack == curStack)
                    return;         // Nothing to do: already handled this control flow + types
                else
                    curStack.mergeWith(*i->knownStack, i->sourceCode);
            }
            i->knownStack = curStack;

            // apply the instruction's effect:
            if (i->word == &_LITERAL) {
                // A literal, just push it
                curStack.push(i->param.param.literal);
            } else if (i->word == &_INT) {
                curStack.push(Value(i->param.param.offset));
            } else if (i->word == &_GETARG || i->word == &_SETARG) {
                // Get/set a function argument. Adjust the offset for the current stack:
                auto offset = i->param.param.offset;
                TypeSet paramType;
                if (offset <= 0)
                    paramType = _effect.inputs()[-offset];
                else
                    paramType = _localsTypes[offset - 1];
                i->param.param.offset -= curStack.depth() - _effect.inputCount();
                if (i->word == &_GETARG)
                    curStack.push(paramType);
                else
                    curStack.add(*i->word, StackEffect({paramType}, {}), i->sourceCode);
            } else if (i->word == &_LOCALS) {
                // Reserving space for local variables:
                for (auto n = i->param.param.offset; n > 0; --n)
                    curStack.push(Value());
            } else if (i->word == &_DROPARGS) {
                // Popping the parameters:
                auto nParams = i->param.param.drop.locals;
                auto nResults = i->param.param.drop.results;
                auto actualResults = curStack.depth() - nParams;
                if (actualResults != nResults)
                    throw compile_error(format("Should return %d values, not %d",
                                               nResults, actualResults), nullptr);
                curStack.erase(nResults, nResults + nParams);
           } else {
                // Determine the effect of a word:
                StackEffect nextEffect = i->word->stackEffect();
                if (nextEffect.isWeird()) {
                    if (i->word == &_RECURSE) {
                        if (_effectCanAddInputs || _effectCanAddOutputs)
                            throw compile_error("RECURSE requires an explicit stack effect declaration",
                                                i->sourceCode);
                        nextEffect = _effect;
                        if (!returnsImmediately(next(i))) {
                            if (_flags & Word::Inline)
                                throw compile_error("Illegal recursion in an inline word",
                                                    i->sourceCode);
                            nextEffect = nextEffect.withUnknownMax();   // non-tail recursion
                        }
                    } else if (i->word == &IFELSE) {
                        nextEffect = effectOfIFELSE(i, curStack);
                    } else {
                        throw compile_error("Oops, don't know word's stack effect", i->sourceCode);
                    }
                }

                if (_effectCanAddInputs) {
                    // We are parsing code with unknown inputs, i.e. a quotation. If the word being
                    // called takes more inputs than are on the stack, make them inputs of this code.
                    const auto nInputs = nextEffect.inputCount();
                    auto nAvailable = curStack.depth();
                    for (auto i = int(nAvailable); i < nInputs; ++i) {
                        auto entry = nextEffect.inputs()[i];
                        curStack.addAtBottom(entry);
                        _effect.addInputAtBottom(entry);
                    }
                }

                // apply the word's effect:
                curStack.add(*i->word, nextEffect, i->sourceCode);
            }

            if (i->word == &_RETURN) {
                // The stack when RETURN is reached determines the word's output effect.
                curStack.checkOutputs(_effect, _effectCanAddOutputs, _effectCanAddOutputTypes);
                _effectCanAddOutputs = false;
                if (curStack.maxGrowth() > _effect.max())
                    _effect = _effect.withMax(int(curStack.maxGrowth()));
                return;

            } else if (i->word == &_BRANCH || i->word == &_ZBRANCH) {
                assert(i->branchTo);
                // If this is a 0BRANCH, recurse to follow the non-branch case too:
                if (i->word == &_ZBRANCH)
                    computeEffect(next(i), curStack);

                // Follow the branch:
                i = *i->branchTo;

            } else {
                // Continue to next instruction:
                ++i;
            }
        }
    }


    StackEffect Compiler::effectOfIFELSE(InstructionPos pos, EffectStack &curStack) {
        // Special case for IFELSE, which has a non-constant stack effect.
        // The two top stack items must be literal quotation values (not just types):
        auto getQuoteEffect = [&](int i) {
            if (auto valP = curStack.literalAt(i); valP) {
                if (auto quote = valP->asQuote(); quote)
                    return quote->stackEffect();
            }
            throw compile_error("IFELSE must be preceded by two quotations", pos->sourceCode);
        };
        StackEffect a = getQuoteEffect(1), b = getQuoteEffect(0);

        // Check if the quotations' effects are compatible, and merge them:
        if (a.net() != b.net())
            throw compile_error("IFELSE quotes have inconsistent stack depths", nullptr);

        StackEffect result = a;

        for (int i = 0; i < b.inputCount(); i++) {
            auto entry = b.inputs()[i];
            if (i < a.inputCount()) {
                entry = entry & result.inputs()[i];
                if (!entry)
                    throw compile_error(format("IFELSE quotes have incompatible parameter #%d", i),
                                        pos->sourceCode);
                result.inputs()[i] = entry;
            } else {
                result.addInput(entry);
            }
        }

        for (int i = 0; i < b.outputCount(); i++) {
            auto entry = b.outputs()[i];
            if (i < a.outputCount()) {
                result.outputs()[i] = result.outputs()[i] | entry;
            } else {
                result.addOutput(entry);
            }
        }

        // Add the inputs of IFELSE itself -- the test and quotes:
        result.addInput(TypeSet::anyType());
        result.addInput(TypeSet(Value::AQuote));
        result.addInput(TypeSet(Value::AQuote));

        return result.withMax( max(0, max(a.max(), b.max()) - 3) );
    }

}
