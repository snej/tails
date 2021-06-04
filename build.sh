#! /bin/bash -e
# Super-stupid build script. Feel free to submit a real make / CMake project :)

echo "Building Tails ..."
cd src
rm -f *.o

compile='cc -std=c++17 -Wall -I . -I core -I values -I compiler'

# Compile core_words.cc with special flags to suppress unnecessary stack frames
$compile -c -O3 -fomit-frame-pointer -fno-stack-check -fno-stack-protector \
    core/core_words.cc more_words.cc

$compile -c {values,compiler}/*.cc

echo "Testing..."
$compile -lc++ *.o test.cc -o ../tails_test
../tails_test >/dev/null || ../tails_test

echo "Building 'tails' REPL ..."
cc -c -I ../vendor/linenoise ../vendor/linenoise/{linenoise,utf8}.c
$compile -c -O3 {values,compiler}/*.cc
$compile -O3 -I ../vendor/linenoise *.o repl.cc -lc++ -o ../tails
rm *.o

echo "Done."
