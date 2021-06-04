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
            auto inputs = initial.inputs();
            for (auto i = inputs.rbegin(); i != inputs.rend(); ++i)
                _stack.emplace_back(*i);
            _maxDepth = _initialDepth = depth();
        }

        size_t depth() const        {return _stack.size();}
        size_t maxGrowth() const    {return _maxDepth - _initialDepth;}

        const Item& at(size_t i) const {
            assert(i < _stack.size());
            return _stack[_stack.size() - 1 - i];
        }

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
        void add(const Word *word, const StackEffect &effect, const char *sourceCode) {
            // Check that the inputs match what's on the stack:
            const auto nInputs = effect.inputCount();
            if (nInputs > depth())
                throw compile_error(format("Calling `%s` would underflow (%zu needed, %zu available)",
                                           word->name(), nInputs, depth()),
                                    sourceCode);
            int i;
            if (auto badType = typeCheck(effect.inputs(), &i); badType)
                throw compile_error(format("Type mismatch passing %s to `%s` (depth %i)",
                                           Value::typeName(*badType), word->name(), i),
                                    sourceCode);

            Item inputs[max(nInputs, 1)];
            for (i = 0; i < nInputs; ++i)
                inputs[i] = at(i);

            _maxDepth = max(_maxDepth, depth() + effect.max());

            // Pop the inputs off the stack:
            _stack.resize(depth() - nInputs);

            // Push the outputs to the stack:
            for (int i = effect.outputCount() - 1; i >= 0; --i) {
                TypeSet ef = effect.outputs()[i];
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

        /// Inserts a type at the _bottom_ of the stack -- used if deducing the input effect.
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
                    mine = itemTypes(mine) | itemTypes(others);
            }
        }

        /// Checks whether the current stack matches a StackEffect's outputs.
        /// if `canAddOutputs` is true, extra items on the stack will be added to the effect.
        void checkOutputs(StackEffect &effect, bool canAddOutputs) const {
            const auto nOutputs = effect.outputCount();
            const auto myDepth = depth();
            if (nOutputs > myDepth)
                throw compile_error(format("Insufficient outputs: have %zu, declared %zu",
                                           myDepth, nOutputs), nullptr);
            // Check effect outputs against stack:
            int i;
            if (auto badType = typeCheck(effect.outputs(), &i); badType)
                throw compile_error(format("Output type mismatch: can't be %s (depth %d)",
                                           Value::typeName(*badType), i), nullptr);

            // Add extra stack items to effect, if allowed:
            for (i = nOutputs; i < myDepth; ++i) {
                if (!canAddOutputs)
                    throw compile_error(format("Too many outputs: have %zu, declared %zu",
                                               myDepth, nOutputs), nullptr);
                auto entry = itemTypes(at(i));
                effect.addOutputAtBottom(entry);
            }
        }

    private:
        static TypeSet itemTypes(const Item &item) {
            if (auto valP = std::get_if<Value>(&item); valP)
                return TypeSet(valP->type());
            else
                return std::get<TypeSet>(item);
        }

        /// Checks if the stack items all match the allowed TypesView;
        /// if not, returns one of the invalid types.
        std::optional<Value::Type> typeCheck(TypesView types, int *outStackIndex) const {
            for (int i = 0; i < types.size(); ++i) {
                TypeSet badTypes = itemTypes(at(i)) - types[i];
                if (auto badType = badTypes.firstType(); badType) {
                    *outStackIndex = i;
                    return badType;
                }
            }
            return std::nullopt;
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
                    const auto nInputs = nextEffect.inputCount();
                    auto nAvailable = curStack.depth();
                    for (auto i = int(nAvailable); i < nInputs; ++i) {
                        auto entry = nextEffect.inputs()[i];
                        curStack.addAtBottom(entry);
                        _effect.addInputAtBottom(entry);
                    }
                }

                // apply the word's effect:
                curStack.add(i->word, nextEffect, i->sourceCode);
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
#endif

}
