#!/bin/bash

for i in *.mzML; do
    tput sgr0;
    echo "Testing $i..."
    ../../mscompress --threads 1 "$i" ./test.msz
    ../../mscompress --threads 1 ./test.msz ./test.mzML
    python3 ../validate.py "$i" ./test.mzML 0 0
    if [ $? -eq 0 ]; then
        tput setab 2; echo "Lossless test $i passed"; tput sgr0;
    else
        tput setab 1; echo "Lossless test $i failed"; tput sgr0;
    fi
    rm -f ./test.msz ./test.mzML
done
