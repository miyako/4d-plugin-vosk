//%attributes = {"invisible":true,"preemptive":"capable"}
#DECLARE($params : Object)

/*

pass null instead of file to transcribe default audio input

*/

var $wav : 4D:C1709.File
$wav:=Folder:C1567(fk desktop folder:K87:19).file("test.wav")
//output is optional

$status:=vosk(Null:C1517; {onData: $params.onData; userData: {window: $params.window}; duration: 9; output: $wav})