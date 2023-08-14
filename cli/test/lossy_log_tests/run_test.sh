#!/bin/bash

for i in *.mzML; do
    tput sgr0;
    echo "Testing $i..."
    ../../mscompress --threads 1 --lossy log "$i" ./test.msz
    ../../mscompress --threads 1 --lossy log ./test.msz ./test.mzML
    python3 ../validate.py "$i" ./test.mzML 0.1 0.1
    if [ $? -eq 0 ]; then
        tput setab 2; echo "Lossy log test $i passed"; tput sgr0;
    else
        tput setab 1; echo "Lossy log test $i failed"; tput sgr0;
    fi
    rm -f ./test.msz ./test.mzML
done
