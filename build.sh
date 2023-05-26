#!/bin/bash
make -j`nproc --all` |& tee ./unit-tests-build-result.txt
exitstatus=$${PIPESTATUS[0]}
echo $$exitstatus > ./build-status.txt

