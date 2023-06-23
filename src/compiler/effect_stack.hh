//
// effect_stack.hh
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
#include "utils.hh"
#include "word.hh"
#include <optional>
#include <variant>

namespace tails {
    using namespace std;


    /// Simulates the runtime stack at compile time, while verifying stack effects.
    class EffectStack {
    public:
        // A stack item can be either a TypeSet (set of types) or a literal Value.
        using Item = variant<TypeSet,Value>;

        EffectStack() = default;
        
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
        void add(const Word &word, const StackEffect &effect, const char *sourceCode) {
            add(word.name(), effect, sourceCode);
        }

        void add(const char *wordName, const StackEffect &effect, const char *sourceCode) {
            // Check that the inputs match what's on the stack:
            const auto nInputs = effect.inputCount();
            if (nInputs > depth())
                throw compile_error(format("Calling `%s` would underflow (%zu needed, %zu available)",
                                           wordName, nInputs, depth()),
                                    sourceCode);
            int i;
            if (auto badType = typeCheck(effect.inputs(), &i); badType)
                throw compile_error(format("Type mismatch passing %s to `%s` (depth %i)",
                                           Value::typeName(*badType), wordName, i),
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

        /// Pushes a type to the stack.
        void add(TypeSet type) {
            _stack.emplace_back(type);
            _maxDepth = max(_maxDepth, depth());
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

        void popParams(size_t nParams, size_t nResults) {
            auto actualResults = _stack.size() - nParams;
            if (actualResults != nResults)
                throw compile_error(format("Should return %d values, not %d", nResults, actualResults), nullptr);
            _stack.erase(_stack.end() - nParams - nResults,
                         _stack.end() - nResults);
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

}
