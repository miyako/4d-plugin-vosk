//%attributes = {}
#DECLARE($status : Object; $userData : Object) : Boolean

If (Caps lock down:C547)
	TRACE:C157
	return True:C214  //stop
End if 

CALL FORM:C1391($userData.window; Formula:C1597(Form:C1466.input:=$status.text))

return False:C215