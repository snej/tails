//
// io.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "stack_effect.hh"
#include <iostream>

namespace tails {

    std::ostream& operator<< (std::ostream&, Value);  // defined in value.cc


    inline std::ostream& operator<< (std::ostream &out, TypeSet entry) {
        if (entry.canBeAnyType())
            out << "x";
        else if (!entry.exists())
            out << "∅";
        else {
            static constexpr const char *kNames[] = {"?", "#", "$", "{}", "[]"};
            for (int i = 0; i <= 5; ++i) {
                if (entry.canBeType(Value::Type(i))) {
                    out << kNames[i];
                }
            }
        }
        return out;
    }


    inline std::ostream& operator<< (std::ostream &out, TypesView types) {
        for (auto i = types.rbegin(); i != types.rend(); ++i) {
            if (i != types.rbegin()) out << ' ';
            out << *i;
        }
        return out;
    }


    inline std::ostream& operator<< (std::ostream &out, const StackEffect &effect) {
        return out << effect.inputs() << " -- " << effect.outputs();
    }


}
