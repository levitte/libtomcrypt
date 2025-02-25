#!/bin/bash

set -e

if [ "$#" = "5" -a "$(echo $3 | grep -v 'makefile[.]')" = "" ]; then
   echo "only run $0 for the regular makefile, early exit success"
   exit 0
fi

if [ -f /proc/cpuinfo ]
then
   MAKE_JOBS=$(( ($(cat /proc/cpuinfo | grep -E '^processor[[:space:]]*:' | tail -n -1 | cut -d':' -f2) + 1) * 2 + 1 ))
else
   MAKE_JOBS=8
fi

# output version
bash .ci/printinfo.sh

make clean &>/dev/null

echo "Build for valgrind..."

# set DWARFv4 as debug format for clang, since it creates DWARFv5 as default which isn't support in old valgrind
[ -z "$(echo $CC | grep "clang")" ] || GFLAG="-gdwarf-4"

make -j$MAKE_JOBS CFLAGS="$2 $CFLAGS $4 $GFLAG" EXTRALIBS="$5" test LTC_DEBUG=1 1>gcc_1.txt 2>gcc_2.txt

echo "Run tests with valgrind..."

for i in `seq 1 10` ; do sleep 300 && echo "Valgrind tests in Progress..."; done &
alive_pid=$!

readonly VALGRIND_OPTS="--error-exitcode=666 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all"

readonly distro="$(lsb_release -si)_$(lsb_release -sc)"
readonly suppfile=".ci/Valgrind-${distro}.supp"
function get_suppfile() { [ -f "$suppfile" ] && echo "--suppressions=$suppfile" || echo ""; }
readonly VALGRIND_EXTRA_OPTS=$(get_suppfile)

valgrind $VALGRIND_OPTS $VALGRIND_EXTRA_OPTS ./test >test_std.txt 2> >(tee -a test_err.txt >&2) || { kill $alive_pid; echo "Valgrind failed"; exit 1; }

kill $alive_pid
