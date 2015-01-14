#!/bin/bash
COMMAND="valgrind --leak-check=full --suppressions=../suppressions.supp smt_decc -v --img --tilesize 32x32 ../data/data.smt"
echo COMMAND = $COMMAND
eval $COMMAND
exit $?
