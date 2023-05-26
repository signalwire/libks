#!/bin/bash
cd /__w/libks/libks
apt-get update && apt-get install -yq build-essential autotools-dev lsb-release pkg-config automake autoconf libtool-bin clang-tools-7
apt-get install -yq cmake uuid-dev libssl-dev colorized-logs
sed -i '/cotire/d' ./CMakeLists.txt
mkdir -p scan-build
scan-build-7 -o ./scan-build/ cmake .
mkdir -p tests/unit/logs
make -j`nproc --all` |& tee ./unit-tests-build-result.txt
exitstatus=${PIPESTATUS[0]}
echo $exitstatus > ./build-status.txt
echo 0 > tests/unit/run-tests-status.txt
env CTEST_OUTPUT_ON_FAILURE=1 make test |& tee >(ansi2html > ./tests/unit/logs/artifacts.html)
exitstatus=${PIPESTATUS[0]}
echo $exitstatus
ls -al
ls -al tests/unit/logs
cat -al tests/unit/logs/artifacts.html
exit $exitstatus
# ./test.sh && exit 0 || echo 'Some tests failed'
# echo 1 > tests/unit/run-tests-status.txt