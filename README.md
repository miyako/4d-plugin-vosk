# 4d-plugin-vosk

```
brew install cmake swig python3
```

```
git clone -b vosk --single-branch --depth=1 https://github.com/alphacep/kaldi.git ~/kaldi
cd ~/kaldi/tools
make openfst cub 
./extras/install_openblas_clapack.sh
cd ../src
./configure --mathlib=OPENBLAS_CLAPACK --shared --static --static-fst --static-math
make -j$(sysctl -n hw.ncpu) online2 lm rnnlm
```
