# 4d-plugin-vosk

- [x] `vosk-win64-0.3.45`

## Examples

```4d
var $wav : 4D.File
$wav:=File("/RESOURCES/test.wav")

var $model : 4D.Folder
$model:=Folder("/RESOURCES/models/vosk-model-small-en-us-0.15/")

var $speaker : 4D.Folder
$speaker:=Folder("/RESOURCES/speakers/vosk-model-spk-0.4/")

$status:=vosk($wav; {model: $model; speaker: $speaker; rate: 16000})
```

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
