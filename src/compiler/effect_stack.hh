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
#include <compare>
#include <optional>
#include <variant>

namespace tails {

    // An EffectStack item can be either a TypeSet (set of types) or a literal Value.
    class TypeItem {
    public:
        TypeItem()                              :_v(TypeSet{}) { }
        explicit TypeItem(TypeSet s)            :_v(s) { }
        explicit TypeItem(Value v)              :_v(v) { }

        bool isLiteral() const                  {return std::holds_alternative<Value>(_v);}
        Value literal() const                   {return std::get<Value>(_v);}
        Value const* if_literal() const         {return std::get_if<Value>(&_v);}

        TypeSet types() const {
            return isLiteral() ? TypeSet(literal().type()) : std::get<TypeSet>(_v);
        }

        friend bool operator==(TypeItem const&, TypeItem const&) = default;

        TypeItem operator| (TypeItem const& b) const {
            return (*this == b) ? *this : TypeItem(this->types() | b.types());
        }

        TypeItem& operator |= (TypeItem const& b) {*this = *this | b; return *this;}

    private:
        std::variant<TypeSet,Value> _v;
    };


    /// Simulates the runtime stack at compile time, while verifying stack effects.
    class EffectStack {
    public:

        EffectStack() = default;
        
        EffectStack(const StackEffect &initial) {
            auto inputs = initial.inputs();
            for (auto i = inputs.rbegin(); i != inputs.rend(); ++i)
                _stack.emplace_back(*i);
            _maxDepth = _initialDepth = depth();
        }

        size_t depth() const        {return _stack.size();}
        size_t maxGrowth() const    {return _maxDepth - _initialDepth;}

        const TypeItem& at(size_t i) const {
            assert(i < _stack.size());
            return _stack[_stack.size() - 1 - i];
        }

        std::optional<Value> literalAt(size_t i) const {
            if (i < depth()) {
                if (auto valP = at(i).if_literal())
                    return *valP;
            }
            return std::nullopt;
        }

        bool operator==(const EffectStack &other) const {
            return _stack == other._stack;
        }

        /// Pushes a type to the stack.
        void push(TypeSet type) {
            _stack.emplace_back(type);
            _maxDepth = std::max(_maxDepth, depth());
        }

        /// Pushes a literal to the stack.
        void push(Value value) {
            _stack.emplace_back(value);
            _maxDepth = std::max(_maxDepth, depth());
        }

        /// Inserts a type at the _bottom_ of the stack -- used if deducing the input effect.
        void addAtBottom(TypeSet entry) {
            _stack.insert(_stack.begin(), TypeItem(entry));
            _maxDepth = std::max(_maxDepth, depth());
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
            if (auto [badTypes, i] = typeCheck(effect.inputs()); badTypes)
                throw compile_error(format("Type mismatch passing %s to `%s` (depth %i)",
                                           badTypes.description().c_str(), wordName, i),
                                    sourceCode);

            std::vector<TypeItem> inputs;
            for (int i = 0; i < nInputs; ++i)
                inputs.push_back(at(i));

            _maxDepth = std::max(_maxDepth, depth() + effect.max());

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

        /// Erases stack items from begin to end (non-inclusive.) Top of stack is 0.
        void erase(size_t begin, size_t end) {
            assert(begin <= end);
            assert(end <= _stack.size());
            _stack.erase(_stack.end() - end, _stack.end() - begin);
        }

        /// Merges myself with another stack -- used when two flows of control join.
        void mergeWith(const EffectStack &other, const char *sourceCode) {
            size_t d = depth();
            if (d != other.depth())
                throw compile_error("Inconsistent stack depth", sourceCode);
            for (size_t i = 0; i < d; ++i)
                at(i) |= other.at(i);
        }

        /// Checks whether the current stack matches a StackEffect's outputs.
        /// if `canAddOutputs` is true, extra items on the stack will be added to the effect.
        void checkOutputs(StackEffect &effect, bool canAddOutputs, bool canAddOutputTypes) const {
            const auto nOutputs = effect.outputCount();
            const auto myDepth = depth();
            if (nOutputs > myDepth)
                throw compile_error(format("Insufficient outputs: have %zu, declared %zu",
                                           myDepth, nOutputs), nullptr);
            // Check effect outputs against stack:
            if (canAddOutputTypes) {
                for (int i = 0; i < nOutputs; ++i)
                    effect.outputs()[i] |= at(i).types();
            } else {
                if (auto [badTypes, i] = typeCheck(effect.outputs()); badTypes)
                    throw compile_error(format("Output type mismatch: can't return %s as %s (depth %d)",
                                               badTypes.description().c_str(),
                                               effect.outputs()[i].description().c_str(), i),
                                        nullptr);
            }

            // Add extra stack items to effect, if allowed:
            for (int i = nOutputs; i < myDepth; ++i) {
                if (!canAddOutputs)
                    throw compile_error(format("Too many outputs: have %zu, declared %zu",
                                               myDepth, nOutputs), nullptr);
                effect.addOutputAtBottom(at(i).types());
            }
        }

    private:
        TypeItem& at(size_t i)  {return _stack[_stack.size() - 1 - i];}

        /// Checks if the stack items all match the allowed TypesView;
        /// if not, returns the invalid types and the stack position.
        std::pair<TypeSet,int> typeCheck(TypesView types) const {
            for (int i = 0; i < types.size(); ++i) {
                if (TypeSet badTypes = at(i).types() - types[i])
                    return {badTypes, i};
            }
            return {};
        }

        std::vector<TypeItem> _stack;
        size_t                _initialDepth = 0;
        size_t                _maxDepth = 0;
    };

}
