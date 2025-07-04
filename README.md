![version](https://img.shields.io/badge/version-20%2B-E23089)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-vosk)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-vosk/total)

# 4d-plugin-vosk

4D implementation of [vosk](https://github.com/alphacep/vosk-api)

## Examples

* set global options
  
```4d
var $model : 4D.Folder
$model:=Folder("/RESOURCES/models/vosk-model-small-en-us-0.15/")
//var $speaker : 4D.Folder
//$speaker:=Folder("/RESOURCES/speakers/vosk-model-spk-0.4/")
//speaker is optional

vosk set options({model: $model; /* speaker: $speaker;*/rate: 16000})
```  



* transcribe the content of a `.wav` file

```4d
var $wav : 4D.File
$wav:=File("/RESOURCES/test.wav")
$status:=vosk($wav)
```

* transcribe audio input with callback, duration, output

```4d
var $wav : 4D.File
$wav:=Folder(fk desktop folder).file("test.wav")
$status:=vosk(Null; {onData: Formula(onData); userData: {window: Current form window; duration: 9; output: $wav})
```

> [!TIP]
> Use large [models](https://alphacephei.com/vosk/models) for better speech recognition
