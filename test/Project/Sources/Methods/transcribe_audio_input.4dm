//%attributes = {"invisible":true,"preemptive":"capable"}
#DECLARE($params : Object)

/*

pass null instead of file to transcribe default audio input

*/

var $model : 4D:C1709.Folder
$model:=Folder:C1567("/RESOURCES/models/vosk-model-small-en-us-0.15/")
var $speaker : 4D:C1709.Folder
$speaker:=Folder:C1567("/RESOURCES/speakers/vosk-model-spk-0.4/")
var $wav : 4D:C1709.File
$wav:=Folder:C1567(fk desktop folder:K87:19).file("test.wav")
$status:=vosk(Null:C1517; {model: $model; speaker: $speaker; rate: 16000; onData: $params.onData; userData: {window: $params.window}; duration: 9; output: $wav})