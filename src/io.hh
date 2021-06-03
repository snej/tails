//
// io.hh
//
// Copyright (C) 2020 Jens Alfke. All Rights Reserved.
//

#pragma once
#include "stack_effect.hh"
#include <iostream>

namespace tails {

    std::ostream& operator<< (std::ostream&, Value);

    inline std::ostream& operator<< (std::ostream &out, TypeSet entry) {
        if (entry.canBeAnyType())
            out << "x";
        else if (!entry.exists())
            out << "∅";
        else {
            static constexpr const char *kNames[] = {"?", "#", "$", "{}", "[]"};
            int n = 0;
            for (int i = 0; i <= 5; ++i) {
                if (entry.canBeType(Value::Type(i))) {
                    out << kNames[i];
                    n++;
                }
            }
        }
        return out;
    }

    inline std::ostream& operator<< (std::ostream &out, const StackEffect &effect) {
        for (int i = effect.inputs() - 1; i >= 0; --i) {
            out << effect.input(i) << ' ';
        }
        out << "--";
        for (int i = effect.outputs() - 1; i >= 0; --i) {
            out << ' ' << effect.output(i);
        }
        return out;
    }


}
