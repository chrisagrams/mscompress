#!/bin/bash

for i in *.mzML; do
    echo "Testing $i..."
    ../../mscompress --threads=1 --lossy=log "$i" ./test.msz
    ../../mscompress --threads=1 --lossy=log ./test.msz ./test.mzML
    python3 validate.py "$i" ./test.mzML
    if [ $? -eq 0 ]; then
        echo -e "\e[1;42m Lossless test $i passed \e[0m"
    else
        echo -e "\e[1;41m Lossless test $i failed \e[0m"
    fi
    rm -f ./test.msz ./test.mzML
done
