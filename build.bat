@echo off
cd loader
make
cd ..
mkdir data
cp loader/loader.bin data/loader.bin
make
