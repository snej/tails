//
// symbol.cc
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

#include "symbol.hh"
#include "io.hh"
#include "parser.hh"
#include <iostream>

namespace tails {
    using namespace std;
    
    
    Symbol::Symbol(Word const& word)
    :token(word.name())
    ,_value(&word)
    { }
    
    Symbol::Symbol(Value val)
    :token("")
    ,_value(val)
    { }
    
    Symbol::Symbol(string const& token)
    :token(token)
    { }
    
    Symbol::~Symbol() = default;
    
    Symbol&& Symbol::makePrefix(priority_t priority) && {
        prefixPriority = priority;
        return move(*this);
    }
    
    Symbol&& Symbol::makePrefix(priority_t priority, Word const& prefixWord) && {
        prefixPriority = priority;
        _prefixWord = &prefixWord;
        return move(*this);
    }
    
    Symbol&& Symbol::makePrefix(priority_t priority, ParsePrefixFn parse) && {
        prefixPriority = priority;
        _customParsePrefix = parse;
        return move(*this);
    }
    
    Symbol&& Symbol::makeInfix(priority_t left, priority_t right) && {
        leftPriority = left;
        rightPriority = right;
        return move(*this);
    }
    
    Symbol&& Symbol::makeInfix(priority_t left, priority_t right, Word const& infixWord) && {
        leftPriority = left;
        rightPriority = right;
        _value = &infixWord;
        return move(*this);
    }
    
    Symbol&& Symbol::makeInfix(priority_t left, priority_t right, ParseInfixFn parse) && {
        leftPriority = left;
        rightPriority = right;
        _customParseInfix = parse;
        return move(*this);
    }
    
    Symbol&& Symbol::makePostfix(priority_t priority) && {
        postfixPriority = priority;
        return move(*this);
    }
    
    Symbol&& Symbol::makePostfix(priority_t priority, ParsePostfixFn parse) && {
        postfixPriority = priority;
        _customParsePostfix = parse;
        return move(*this);
    }

    StackEffect Symbol::parsePrefix(Parser& parser) const {
        if (_customParsePrefix)
            return _customParsePrefix(parser);
        
        StackEffect lhsEffect = parser.nextExpression(prefixPriority);
        
        auto &word = _prefixWord ? *_prefixWord : this->word();
        assert(word.stackEffect().inputCount() == lhsEffect.outputCount());
        parser.compileCall(word);
        return lhsEffect | word.stackEffect();
    }
    
    StackEffect Symbol::parseInfix(StackEffect const& lhsEffect, Parser& parser) const {
        if (_customParseInfix)
            return _customParseInfix(lhsEffect, parser);
        
        StackEffect rhsEffect = parser.nextExpression(leftPriority);
        
        StackEffect inputEffect = lhsEffect | rhsEffect;
        //        cout << "Infix 1: combined " << lhsEffect << " | " << rhsEffect << " --> " << inputEffect << endl;
        auto &word = this->word();
        assert(word.stackEffect().inputCount() == inputEffect.outputCount());
        parser.compileCall(word);
        auto result = inputEffect | word.stackEffect();
        //        cout << "Infix 2: combined " << inputEffect << " | " << word.stackEffect() << " --> " << result << endl;
        //        cout << "Infix effect is " << result << endl;//TEMP
        return result;
    }
    
    StackEffect Symbol::parsePostfix(StackEffect const& lhsEffect, Parser& parser) const {
        if (_customParsePostfix)
            return _customParsePostfix(lhsEffect, parser);
        
        // same as parsePrefix but without the call to nextExpression since the LHS has been parsed.
        auto &word = this->word();
        assert(word.stackEffect().inputCount() == lhsEffect.outputCount());
        parser.compileCall(word);
        return lhsEffect | word.stackEffect();
    }
    
    
#pragma mark - SYMBOL TABLE:
    
    
    void SymbolTable::addPtr(unique_ptr<Symbol> symbol) {
        _registry.emplace(toupper(symbol->token), move(symbol));
    }
    
    Symbol const* SymbolTable::get(string_view literal) const {
        if (auto i = _registry.find(toupper(literal)); i != _registry.end())
            return i->second.get();
        else if (_parent)
            return _parent->get(literal);
        else
            return nullptr;
    }
    
    bool SymbolTable::itselfHas(std::string_view name) const {
        return _registry.find(toupper(name)) != _registry.end();
    }
    
}
