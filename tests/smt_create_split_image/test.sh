#!/bin/bash
VALGRIND="valgrind --leak-check=full --suppressions=../suppressions.supp"
COMMAND="$VALGRIND smt_decc -v --smt --tilesize 32x32 --imagesize 1024x1024 ../data/image_1.png"
echo COMMAND = $COMMAND
eval $COMMAND
exit $?