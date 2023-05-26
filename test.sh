#!/bin/bash
env CTEST_OUTPUT_ON_FAILURE=1 make test |& tee >(ansi2html > ./tests/unit/logs/artifacts.html)
exitstatus=$${PIPESTATUS[0]}
exit $$exitstatus

