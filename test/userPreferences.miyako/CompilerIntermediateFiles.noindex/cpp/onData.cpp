extern Txt Kwindow;
extern unsigned char D_proc_ONDATA[];
void proc_ONDATA( Asm4d_globals *glob, tProcessGlobals *ctx, int32_t inNbExplicitParam, int32_t inNbParam, PCV inParams[], CV *outResult)
{
	CallChain c(ctx,D_proc_ONDATA);
	if (!ctx->doingAbort && c.f.fLine==0) {
		new ( outResult) Bool();
		{
			Bool t0;
			c.f.fLine=3;
			if (g->Call(ctx,(PCV[]){t0.cv()},0,547)) goto _0;
			if (!(t0.get())) goto _2;
		}
		c.f.fLine=5;
		Res<Bool>(outResult)=Bool(1).get();
		asm volatile("");
		goto _0;
_2:
		{
			Variant t1;
			c.f.fLine=8;
			if (g->GetMember(ctx,Parm<Obj>(ctx,inParams,inNbParam,2).cv(),Kwindow.cv(),t1.cv())) goto _0;
			asm volatile("");
			goto _3;
_3:
			Obj t5;
			if (g->Call(ctx,(PCV[]){t5.cv(),Long(0).cv(),(CV*)-1,nullptr,Long(1).cv(),Long(1).cv(),Parm<Obj>(ctx,inParams,inNbParam,1).cv()},6,1597)) goto _0;
			g->Check(ctx);
			if (g->Call(ctx,(PCV[]){nullptr,t1.cv(),t5.cv()},2,1391)) goto _0;
		}
		if (ctx->doingAbort) goto _0;
		c.f.fLine=10;
		Res<Bool>(outResult)=Bool(0).get();
		asm volatile("");
		goto _0;
_0:
_1:
;
	}

}
