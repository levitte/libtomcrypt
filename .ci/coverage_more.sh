#!/bin/bash

set -e

if [ "$#" = "1" -a "$(echo $1 | grep 'gmp')" != "" ]; then
   ./test t gmp
fi

./sizes
./constants

for i in $(for j in $(echo $(./hashsum -h | awk '/Algorithms/,EOF' | tail -n +2)); do echo $j; done | sort); do echo -n "$i: " && ./hashsum -a $i tests/test.key ; done > hashsum_tv.txt
difftroubles=$(diff -i -w -B hashsum_tv.txt notes/hashsum_tv.txt | grep '^<') || true
if [ -n "$difftroubles" ]; then
  echo "FAILURE: hashsum_tv.tx"
  diff -i -w -B hashsum_tv.txt notes/hashsum_tv.txt
  echo "hashsum failed"
  exit 1
else
  echo "hashsum okay"
fi


exit 0
