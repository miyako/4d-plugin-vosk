extern Txt K;
extern Txt Kcode;
extern Txt Kinput;
extern Txt KonData;
extern Txt Kvosk;
extern Txt Kwindow;
extern Txt kMMDED5Q0cTI;
extern unsigned char D_obj_p_4_test_00Button[];
void obj_p_4_test_00Button( Asm4d_globals *glob, tProcessGlobals *ctx, int32_t inNbExplicitParam, int32_t inNbParam, PCV inParams[], CV *outResult)
{
	CallChain c(ctx,D_obj_p_4_test_00Button);
	if (!ctx->doingAbort && c.f.fLine==0) {
		{
			Obj t0;
			c.f.fLine=1;
			if (g->Call(ctx,(PCV[]){t0.cv()},0,1606)) goto _0;
			g->Check(ctx);
			Variant t1;
			if (g->GetMember(ctx,t0.cv(),Kcode.cv(),t1.cv())) goto _0;
			Bool t2;
			if (g->OperationOnAny(ctx,6,t1.cv(),Long(4).cv(),t2.cv())) goto _0;
			if (!(t2.get())) goto _2;
		}
		{
			Obj t3;
			c.f.fLine=3;
			if (g->Call(ctx,(PCV[]){t3.cv()},0,1466)) goto _0;
			if (g->SetMember(ctx,t3.cv(),Kinput.cv(),K.cv())) goto _0;
		}
		{
			Long t4;
			c.f.fLine=5;
			if (g->Call(ctx,(PCV[]){t4.cv()},0,827)) goto _0;
			g->Check(ctx);
			Obj t5;
			if (g->Call(ctx,(PCV[]){t5.cv(),Kwindow.cv(),t4.cv(),KonData.cv(),KonData.cv()},4,1471)) goto _0;
			if (g->Call(ctx,(PCV[]){nullptr,Kvosk.cv(),kMMDED5Q0cTI.cv(),t5.cv()},3,1389)) goto _0;
		}
_2:
_0:
_1:
;
	}

}
