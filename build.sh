#! /bin/bash -e
# Super-stupid build script. Feel free to submit a real make / CMake project :)

echo "Building Tails ..."
cd src
rm -f *.o

CC=clang
CPP=clang++
# The following lines let me force GCC, for compatibility checking. --Jens
#CC=/usr/local/bin/gcc-11
#CPP=/usr/local/bin/g++-11

compile="$CPP -std=c++17 -I . -I core -I values -I compiler -Wall -Wno-sign-compare"

# Compile core_words.cc with special flags to suppress unnecessary stack frames
$compile -c -O3 -fomit-frame-pointer -fno-stack-check -fno-stack-protector \
    core/core_words.cc more_words.cc

$compile -c {values,compiler}/*.cc

echo "Testing..."
$compile *.o test.cc -o ../tails_test
../tails_test >/dev/null || ../tails_test

echo "Building 'tails' REPL ..."
$CC -c -I ../vendor/linenoise ../vendor/linenoise/{linenoise,utf8}.c
$compile -c -O3 {values,compiler}/*.cc
$compile -O3 -I ../vendor/linenoise *.o repl.cc -o ../tails
rm *.o

echo "Done."
