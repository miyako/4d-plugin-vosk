If (FORM Event:C1606.code=On Clicked:K2:4)
	
	Form:C1466.input:=""
	
	CALL WORKER:C1389("vosk"; "transcribe_file"; {window: Current form window:C827; onData: "onData"})
	
End if 