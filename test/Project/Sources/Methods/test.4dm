//%attributes = {}
var $wav : 4D:C1709.File
$wav:=File:C1566("/RESOURCES/test.wav")

var $model : 4D:C1709.Folder
$model:=Folder:C1567("/RESOURCES/models/vosk-model-small-en-us-0.15/")

var $speaker : 4D:C1709.Folder
$speaker:=Folder:C1567("/RESOURCES/speakers/vosk-model-spk-0.4/")

$status:=vosk($wav; {model: $model; speaker: $speaker; rate: 16000})
