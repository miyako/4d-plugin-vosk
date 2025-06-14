## Notes

```
brew install cmake swig python3
```

```
git clone -b vosk --single-branch --depth=1 https://github.com/alphacep/kaldi.git ~/kaldi
cd ~/kaldi/tools
make openfst cub 
./extras/install_openblas_clapack.sh
cd ../src
./configure --mathlib=OPENBLAS --shared --static --static-fst --static-math
make -j$(sysctl -n hw.ncpu) online2 lm rnnlm
```

```
cd vosk-api/src
export KALDI_ROOT=~/kaldi 
make -j$(sysctl -n hw.ncpu)
ar rcs libvosk.a *.o  
```
