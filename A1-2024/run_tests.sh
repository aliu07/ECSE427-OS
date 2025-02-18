#!/bin/bash
testdir="$(pwd)/test-cases"

if [ -n "$1" ]; then
  mkdir tmp
  cd tmp
  ../starter-code/mysh < "$testdir/$1.txt" > ../output
  diff ../output "$testdir/${testname}_result.txt" -qw
  cd ..
  rm -r tmp
else

for testname in $(cat test-names); do
  mkdir tmp
  cd tmp
  ../starter-code/mysh < "$testdir/$testname.txt" > output
  diff output "$testdir/${testname}_result.txt" -qw
  cd ..
  rm -r tmp
done

fi
