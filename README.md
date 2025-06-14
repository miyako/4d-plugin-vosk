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
