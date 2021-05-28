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
#include "utils.hh"
#include <optional>
#include <sstream>
#include <string>
#include <variant>

namespace tails {
    using namespace std;
    using namespace tails::core_words;


#pragma mark - EFFECTSTACK
    

    /// Simulates the runtime stack at compile time, while verifying stack effects.
    class Compiler::EffectStack {
    public:
        // A stack item can be either a TypeSet (set of types) or a literal Value.
        using Item = variant<TypeSet,Value>;

        EffectStack(const StackEffect &initial) {
            for (int i = 0; i < initial.inputs(); ++i)
                _stack.emplace_back(initial.input(i));
            _maxDepth = _initialDepth = depth();
        }

        size_t depth() const {return _stack.size();}
        size_t maxGrowth() const {return _maxDepth - _initialDepth;}

        const Item& at(size_t i) const {return _stack[_stack.size() - 1 - i];}

        optional<Value> literalAt(size_t i) const {
            if (i < depth()) {
                if (auto valP = std::get_if<Value>(&at(i)); valP)
                    return *valP;
            }
            return nullopt;
        }

        bool operator==(const EffectStack &other) const {
            return _stack == other._stack;
        }

        /// Adds the stack effect of calling a word. Throws an exception on failure.
        void add(const StackEffect &effect, const Word *word, const char *sourceCode) {
            // Check that the inputs match what's on the stack:
            const auto nInputs = effect.inputs();
            if (nInputs > depth())
                throw compile_error(format("Stack underflow calling `%s`: %zu needed, %zu available",
                                           word->name(), nInputs, depth()),
                                    sourceCode);
            Item inputs[max(nInputs, 1)];
            for (int i = 0; i < nInputs; ++i) {
                auto input = effect.input(i);
                auto &item = at(i);
                inputs[i] = item;
                optional<Value::Type> badType;
                if (auto valP = std::get_if<Value>(&item); valP) {
                    if (!input.canBeType(valP->type())) {
                        badType = valP->type();
                    }
                } else {
                    badType = (std::get<TypeSet>(item) - input).firstType();
                }
                if (badType)
                    throw compile_error(format("Type mismatch passing %s to `%s` at depth %zu",
                                               Value::typeName(*badType), word->name(), i),
                                        sourceCode);

            }

            _maxDepth = max(_maxDepth, depth() + effect.max());

            // Pop the inputs off the stack:
            _stack.resize(depth() - nInputs);

            // Push the outputs to the stack:
            for (int i = effect.outputs() - 1; i >= 0; --i) {
                TypeSet ef = effect.output(i);
                if (auto in = ef.inputMatch(); in >= 0)
                    _stack.emplace_back(inputs[in]);
                else
                    _stack.emplace_back(ef);
            }
        }

        /// Pushes a literal to the stack.
        void add(Value value) {
            _stack.emplace_back(value);
            _maxDepth = max(_maxDepth, depth());
        }

        void addAtBottom(TypeSet entry) {
            _stack.insert(_stack.begin(), entry);
            _maxDepth = max(_maxDepth, depth());
        }

        /// Merges myself with another stack -- used when two flows of control join.
        void mergeWith(const EffectStack &other, const char *sourceCode) {
            size_t d = depth();
            if (d != other.depth())
                throw compile_error("Inconsistent stack depth", sourceCode);
            for (size_t i = 0; i < d; ++i) {
                Item &mine = _stack[_stack.size() - 1 - i];
                const Item others = other.at(i);
                if (others != mine)
                    mine = itemEntry(mine) | itemEntry(others);
            }
        }

        /// Checks whether the current stack matches a StackEffect's outputs.
        /// if `canAddOutputs` is true, extra items on the stack will be added to the effect.
        void checkOutputs(StackEffect &effect, bool canAddOutputs) const {
            const auto nOutputs = effect.outputs();
            const auto myDepth = depth();
            if (nOutputs > myDepth)
                throw compile_error(format("Insufficient outputs: have %zu, declared %zu",
                                           myDepth, nOutputs), nullptr);
            // Check effect outputs against stack:
            for (int i = 0; i < nOutputs; ++i) {
                auto ef = effect.output(i);
                auto &item = at(i);
                if (auto valP = std::get_if<Value>(&item); valP) {
                    if (!ef.canBeType(valP->type()))
                        throw compile_error("Output type mismatch", nullptr);
                } else {
                    if (std::get<TypeSet>(item) > ef)
                        throw compile_error("Output type mismatch", nullptr);
                }
            }

            // Add extra stack items to effect, if allowed:
            for (int i = nOutputs; i < myDepth; ++i) {
                if (!canAddOutputs)
                    throw compile_error(format("Too many outputs: have %zu, declared %zu",
                                               myDepth, nOutputs), nullptr);
                auto entry = itemEntry(at(i));
                effect.addOutputAtBottom(entry);
            }
        }

    private:
        static TypeSet itemEntry(const Item &item) {
            if (auto valP = std::get_if<Value>(&item); valP)
                return TypeSet(valP->type());
            else
                return std::get<TypeSet>(item);
        }

        std::vector<Item> _stack;
        size_t            _initialDepth = 0;
        size_t            _maxDepth = 0;
    };


#pragma mark - SOURCEWORD:


    /// Extension of WordRef that adds private fields used by the compiler.
    struct Compiler::SourceWord : public Compiler::WordRef {
        SourceWord(const WordRef &ref, const char *source =nullptr)
        :WordRef(ref)
        ,sourceCode(source)
        { }

        const char*  sourceCode;                        // Points to source code where word appears
        std::optional<EffectStack> knownStack;          // Stack effect at this point, once known
        std::optional<InstructionPos> branchDestination;// Points to where a branch goes
        int pc;                                         // Relative address during code-gen
    };


#pragma mark - COMPILER's STACK CHECKER:


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
                curStack.add(i->param.literal);
            } else {
                // Determine the effect of a word:
                StackEffect nextEffect = i->word->stackEffect();
                if (nextEffect.isWeird()) {
#ifndef SIMPLE_VALUE
                    if (i->word == &IFELSE)
                        nextEffect = effectOfIFELSE(i, curStack);
                    else
#endif
                        throw compile_error("Oops, don't know word's stack effect", i->sourceCode);
                }

                if (_effectCanAddInputs) {
                    // We are parsing code with unknown inputs, i.e. a quotation. If the word being
                    // called takes more inputs than are on the stack, make them inputs of this code.
                    const auto nInputs = nextEffect.inputs();
                    auto nAvailable = curStack.depth();
                    for (auto i = int(nAvailable); i < nInputs; ++i) {
                        auto entry = nextEffect.input(i);
                        curStack.addAtBottom(entry);
                        _effect.addInputAtBottom(entry);
                    }
                }

                // apply the word's effect:
                curStack.add(nextEffect, i->word, i->sourceCode);
            }

            if (i->word == &_RETURN) {
                // The stack when RETURN is reached determines the word's output effect.
                curStack.checkOutputs(_effect, _effectCanAddOutputs);
                _effectCanAddOutputs = false;
                if (curStack.maxGrowth() > _effect.max())
                    _effect = _effect.withMax(int(curStack.maxGrowth()));
                return;

            } else if (i->word == &_BRANCH || i->word == &_ZBRANCH) {
                assert(i->branchDestination);
                // If this is a 0BRANCH, recurse to follow the non-branch case too:
                if (i->word == &_ZBRANCH)
                    computeEffect(next(i), curStack);

                // Follow the branch:
                i = *i->branchDestination;

            } else {
                // Continue to next instruction:
                ++i;
            }
        }
    }


#ifndef SIMPLE_VALUE
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

        for (int i = 0; i < b.inputs(); i++) {
            auto entry = b.input(i);
            if (i < a.inputs()) {
                entry = entry & result.input(i);
                if (!entry)
                    throw compile_error(format("IFELSE quotes have incompatible parameter #%d", i),
                                        pos->sourceCode);
                result.input(i) = entry;
            } else {
                result.addInput(entry);
            }
        }

        for (int i = 0; i < b.outputs(); i++) {
            auto entry = b.output(i);
            if (i < a.outputs()) {
                result.output(i) = result.output(i) | entry;
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
#endif

}
