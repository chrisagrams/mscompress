#!/bin/bash

for i in *.mzML; do
    tput sgr0;
    echo "Testing $i..."
    ../../mscompress --mz-lossy delta32 "$i" ./test.msz
    ../../mscompress ./test.msz ./test.mzML
    python3 ../validate.py "$i" ./test.mzML 0.1 0.1
    if [ $? -eq 0 ]; then
        tput setab 2; echo "delta test $i passed"; tput sgr0;
    else
        tput setab 1; echo "delta test $i failed"; tput sgr0;
    fi
    rm -f ./test.msz ./test.mzML
done
