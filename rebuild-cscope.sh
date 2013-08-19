#!/bin/bash

find  -name '*.c' -o -name '*.h' | egrep -v '\./(stage|tools|build)/' > cscope.files
cscope -b -q -k
