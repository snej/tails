//
// effect_stack.cc
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

#include "effect_stack.hh"
#include "io.hh"
#include <iostream>

namespace tails {

    std::ostream& operator<< (std::ostream& out, TypeItem const& item) {
        if (item.isLiteral())
            return out << '`' << item.literal() << "`";
        else
            return out << item.types();
    }

    std::ostream& operator<< (std::ostream& out, EffectStack const& stack) {
        out << "EffectStack[";
        for (TypeItem const& item : stack._stack) {
            out << item << ' ';
        }
        return out << "]";
    }

    std::string EffectStack::dump() const {
        std::stringstream out;
        out << *this;
        return out.str();
    }


}
