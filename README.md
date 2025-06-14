![version](https://img.shields.io/badge/version-20%2B-E23089)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-vosk)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-vosk/total)

# 4d-plugin-vosk

4D implementation of [vosk](https://github.com/alphacep/vosk-api)

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
