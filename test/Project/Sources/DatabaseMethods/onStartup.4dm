var $model : 4D:C1709.Folder
$model:=Folder:C1567("/RESOURCES/models/vosk-model-small-en-us-0.15/")
//var $speaker : 4D.Folder
//$speaker:=Folder("/RESOURCES/speakers/vosk-model-spk-0.4/")
//speaker is optional

vosk set options({model: $model; /* speaker: $speaker;*/rate: 16000})