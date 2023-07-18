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
#include <iosfwd>
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
            if (isLiteral()) {
                auto type = literal().type();
                TypeSet result(type);
                if (type == Value::AQuote)
                    result.withQuoteEffect(literal().asQuote()->stackEffect());
                return result;
            } else {
                return std::get<TypeSet>(_v);
            }
        }

        friend bool operator==(TypeItem const&, TypeItem const&) = default;

        TypeItem operator| (TypeItem const& b) const {
            return (*this == b) ? *this : TypeItem(this->types() | b.types());
        }

        TypeItem& operator |= (TypeItem const& b) {*this = *this | b; return *this;}

        friend std::ostream& operator<< (std::ostream&, TypeItem const&);

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

        /// The current stack depth.
        size_t depth() const        {return _stack.size();}

        /// The maximum stack depth relative to the initial depth.
        size_t maxGrowth() const    {return _maxDepth - _initialDepth;}

        /// Returns a stack item. Index 0 is top of stack, 1 is below that...
        const TypeItem& at(size_t i) const {
            if (i >= _stack.size())
                throw compile_error("Stack underflow");
            return _stack[_stack.size() - 1 - i];
        }

        /// Returns a stack item. Index 0 is top of stack, 1 is below that...
        TypeItem const& operator[] (size_t i) const   {return at(i);}

        /// Returns a stack item as a literal Value, or else nullopt.
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

        /// Pushes a type or literal to the stack.
        void push(auto item) {
            _stack.emplace_back(item);
            markMaxDepth();
        }

        /// Pops the top of the stack, returning the TypeItem.
        TypeItem pop() {
            TypeItem top = at(0);
            _stack.pop_back();
            return top;
        }

        /// Pushes the item at depth 'n'.
        void over(int n) {
            push(at(n));
        }

        /// Arbitrary stack rotation. Positive n moves item at depth n to the top of the stack;
        /// negative n moves the top of the stack to depth n. Emulates the ROTn instruction.
        void rotate(int n) {
            if (depth() < n)
                throw compile_error("Stack underflow");
            if (n > 0) {
                _stack.push_back(_at(n));
                _stack.erase(_stack.end() - 1 - n - 1);
            } else if (n < 0) {
                _stack.insert(_stack.end() + n - 1, _stack.back());
                _stack.pop_back();
            }
       }

        /// Inserts a type at the _bottom_ of the stack -- used if deducing the input effect.
        void addAtBottom(TypeSet entry) {
            _stack.insert(_stack.begin(), TypeItem(entry));
            _initialDepth++;
            _maxDepth++;
        }

        /// Adds the stack effect of calling a word. Throws an exception if the stack contains too
        /// few inputs or the wrong types.
        void add(const Word &word, const StackEffect &effect) {
            assert(!effect.isWeird());
            // Check that the inputs match what's on the stack:
            const auto nInputs = effect.inputCount();
            if (nInputs > depth())
                throw compile_error(format("Calling `%s` would underflow (%zu needed, %zu available)",
                                           word.name(), nInputs, depth()));
            if (auto [badTypes, i] = typeCheck(effect.inputs()); badTypes)
                throw compile_error(format("Type mismatch passing %s to `%s` (depth %i)",
                                           badTypes.description().c_str(), word.name(), i));

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

        /// Changes the type of an item in the stack. Used when first setting a local variable.
        void setTypeAt(int index, TypeSet type) {
            assert(index < _stack.size());
            _at(index) = TypeItem(type);
        }

        /// Erases stack items from begin to end (non-inclusive.) Top of stack is 0.
        void erase(size_t begin, size_t end) {
            assert(begin <= end);
            if (end > _stack.size())
                throw compile_error("Stack underflow");
            _stack.erase(_stack.end() - end, _stack.end() - begin);
        }

        /// Merges myself with another stack -- used when two flows of control join.
        /// Throws an exception if the depths don't match.
        void mergeWith(const EffectStack &other) {
            size_t d = depth();
            if (d != other.depth())
                throw compile_error("Inconsistent stack depth");
            for (size_t i = 0; i < d; ++i)
                _at(i) |= other.at(i);
            _maxDepth = std::max(_maxDepth, other._maxDepth);
        }

        /// Checks whether the current stack matches a StackEffect's outputs.
        /// if `canAddOutputs` is true, extra items on the stack will be added to the effect.
        /// @param effect  The StackEffect to check (only its outputs).
        /// @param canAddOutputs  If true, excess items on the stack will be added as outputs.
        /// @param canAddOutputTypes  If true, then if the stack types don't match the effect's
        ///             outputs, the outputs will be broadened. (Otherwise an exception is thrown.)
        void checkOutputs(StackEffect &effect, bool canAddOutputs, bool canAddOutputTypes) const {
            const auto nOutputs = effect.outputCount();
            const auto myDepth = depth();
            if (nOutputs > myDepth)
                throw compile_error(format("Insufficient outputs: have %zu, declared %zu",
                                           myDepth, nOutputs));
            // Check effect outputs against stack:
            if (canAddOutputTypes) {
                for (int i = 0; i < nOutputs; ++i)
                    effect.outputs()[i] |= at(i).types();
            } else {
                if (auto [badTypes, i] = typeCheck(effect.outputs()); badTypes)
                    throw compile_error(format("Output type mismatch: can't return %s as %s (depth %d)",
                                               badTypes.description().c_str(),
                                               effect.outputs()[i].description().c_str(), i));
            }

            // Add extra stack items to effect, if allowed:
            for (int i = nOutputs; i < myDepth; ++i) {
                if (!canAddOutputs)
                    throw compile_error(format("Too many outputs: have %zu, declared %zu",
                                               myDepth, nOutputs));
                effect.addOutputAtBottom(at(i).types());
            }
        }

        friend std::ostream& operator<< (std::ostream&, EffectStack const&);
        std::string dump() const;

    private:
        TypeItem& _at(size_t i)  {return _stack[_stack.size() - 1 - i];}

        void markMaxDepth() {_maxDepth = std::max(_maxDepth, depth());}

        /// Checks if the stack items all match the allowed TypesView;
        /// if not, returns the invalid types and the stack position.
        std::pair<TypeSet,int> typeCheck(std::vector<TypeSet> const& types) const {
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
