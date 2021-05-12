#! /bin/sh -e

echo "Building tails_test ..."
rm -f *.o

# Compile core_words.cc with special flags to suppress unnecessary stack frames
cc -c -std=c++17 -Wall -O3 -fomit-frame-pointer -fno-stack-check -fno-stack-protector src/core_words.cc

cc -c -std=c++17 -Wall src/{vocabulary,compiler,test}.cc
cc -lc++ *.o -o tails_test

echo "Testing..."
./tails_test >/dev/null || ./tails_test

echo "Done."
