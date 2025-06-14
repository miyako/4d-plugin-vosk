extern Txt Kmodel;
extern Txt KonData;
extern Txt Krate;
extern Txt Kspeaker;
extern Txt KuserData;
extern Txt Kwindow;
extern Txt kBJ8LAp7xgqo;
extern Txt kdcoz0hy7B$8;
extern Txt kiypietGJcyQ;
extern unsigned char D_proc_TRANSCRIBE__FILE[];
void proc_TRANSCRIBE__FILE( Asm4d_globals *glob, tProcessGlobals *ctx, int32_t inNbExplicitParam, int32_t inNbParam, PCV inParams[], CV *outResult)
{
	CallChain c(ctx,D_proc_TRANSCRIBE__FILE);
	if (!ctx->doingAbort && c.f.fLine==0) {
		Obj lwav;
		Obj lmodel;
		Obj lspeaker;
		Obj lstatus;
		{
			Obj t0;
			c.f.fLine=10;
			if (g->Call(ctx,(PCV[]){t0.cv(),kdcoz0hy7B$8.cv()},1,1566)) goto _0;
			g->Check(ctx);
			lwav=t0.get();
		}
		{
			Obj t1;
			c.f.fLine=12;
			if (g->Call(ctx,(PCV[]){t1.cv(),kBJ8LAp7xgqo.cv()},1,1567)) goto _0;
			g->Check(ctx);
			lmodel=t1.get();
		}
		{
			Obj t2;
			c.f.fLine=14;
			if (g->Call(ctx,(PCV[]){t2.cv(),kiypietGJcyQ.cv()},1,1567)) goto _0;
			g->Check(ctx);
			lspeaker=t2.get();
		}
		{
			Variant t3;
			c.f.fLine=15;
			if (g->GetMember(ctx,Parm<Obj>(ctx,inParams,inNbParam,1).cv(),KonData.cv(),t3.cv())) goto _0;
			Variant t4;
			if (g->GetMember(ctx,Parm<Obj>(ctx,inParams,inNbParam,1).cv(),Kwindow.cv(),t4.cv())) goto _0;
			Obj t5;
			if (g->Call(ctx,(PCV[]){t5.cv(),Kwindow.cv(),t4.cv()},2,1471)) goto _0;
			g->Check(ctx);
			Obj t6;
			if (g->Call(ctx,(PCV[]){t6.cv(),Kmodel.cv(),lmodel.cv(),Kspeaker.cv(),lspeaker.cv(),Krate.cv(),Long(16000).cv(),KonData.cv(),t3.cv(),KuserData.cv(),t5.cv()},10,1471)) goto _0;
			Obj t7;
			t7=lwav.get();
			Obj t8;
			if (g->CallPlugin(ctx,(PCV[]){Ref(1,1).cv(),t8.cv(),t7.cv(),lwav.cv(),(CV*)38,t6.cv(),nullptr,(CV*)38},2)) goto _0;
			lstatus=t8.get();
		}
		if (ctx->doingAbort) goto _0;
_0:
_1:
;
	}

}
