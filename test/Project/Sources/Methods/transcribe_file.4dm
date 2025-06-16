//%attributes = {"invisible":true,"preemptive":"capable"}
#DECLARE($params : Object)

/*

transcribe the contents of a .wav audio file using a model

*/

var $wav : 4D:C1709.File
$wav:=File:C1566("/RESOURCES/test.wav")
$status:=vosk($wav; {onData: $params.onData; userData: {window: $params.window}})