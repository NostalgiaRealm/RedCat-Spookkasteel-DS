// Davi-Script VM: the compiled level scripts of the original game, as converted to compact bytecode.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {u8 type; union {s32 i; float d; const char *s;};} Value;  // 1 double, 2 int, 3 string, 4 object (>=0 binding, <0 ~object)
static Value *globals; int vmBudget,vmUnknown; u32 keyItems[8];
static char strPool[8][96]; static int strNext;
const char *str_at(u32 id){return gd.strData+gd.strOff[id];}
static const char *pooled(const char *a,const char *b){char *d=strPool[strNext++&7]; snprintf(d,96,"%s%s",a,b); return d;}
void vm_init(void){
  memset(keyItems,0,sizeof keyItems);
  free(globals); globals=calloc(gd.nglob+1,sizeof(Value));
  const u8 *g=gd.globs;
  for(u32 i=0;i<gd.nglob;i++,g+=8){u8 t=g[0]; s32 v; memcpy(&v,g+4,4); globals[i].type=t; globals[i].i=v; if(t==3)globals[i].s=str_at(v);}
}
void *vm_globals(u32 *bytes){*bytes=gd.nglob*sizeof(Value);return globals;}
// Text in saved variables points into the old copy of the script data.
void vm_rebase(const char *oldBase){for(u32 i=0;i<gd.nglob;i++)if(globals[i].type==3){const char *s=globals[i].s; const u32 size=gd.strOff[gd.nstr-1]+64; globals[i].s=(s>=oldBase&&s<oldBase+size)?gd.strData+(s-oldBase):gd.strData+gd.strOff[0];}}
static Value call(int fn,Value *args,int nargs,s32 self,bool hasSelf);
typedef struct {Value slots[12],regs[12],stack[12]; int sp;} Frame;
static Value coerce(int t,Value v){
  if(t==v.type||!t)return v;
  Value r=v; r.type=t;
  if(t==2&&v.type==1)r.i=(s32)v.d; else if(t==1&&v.type==2)r.d=(float)v.i; return r;
}
static Value rd(const Expr *e,Frame *f){
  Value v; v.type=e->type; v.i=0;
  switch(e->tag){
    case 0: if(e->type==3)v.s=str_at(e->val); else if(e->type==4)v.i=-0x7fffffff; else v.i=e->val; return v;
    case 1: if(f->sp>0)return f->stack[--f->sp]; return v;
    case 5: return f->regs[e->val&11];
    case 6: return f->slots[e->val];
    case 7: return globals[e->val];
    case 8: v.type=4; v.i=e->val; return v;
    case 4: {
      const FuncDef *fd=&gd.funcs[e->val]; Value a[8]; int n=fd->nparams; s32 self=-0x7fffffff; bool has=false; Value vals[8];
      for(int i=n-1;i>=0;i--)vals[i]=f->sp>0?f->stack[--f->sp]:(Value){0};
      int k=0; for(int i=0;i<n;i++){ if(i==fd->selfIndex){self=vals[i].i;has=true;} else a[k++]=vals[i]; }
      return call(e->val,a,k,self,has);
    }
  }
  return v;
}
static void wr(const Expr *e,Value v,Frame *f){
  switch(e->tag){
    case 1: if(f->sp<12)f->stack[f->sp++]=coerce(e->type,v); break;
    case 5: f->regs[e->val&11]=v; break;
    case 6: f->slots[e->val]=coerce(e->type,v); break;
    case 7: globals[e->val]=coerce(e->type,v); break;
  }
}
static Value binary(int op,Value a,Value b){
  Value r; r.type=2; r.i=0;
  if(a.type==3&&b.type==3){
    int c=strcmp(a.s,b.s);
    if(op==1){r.type=3;r.s=pooled(a.s,b.s);return r;}
    r.i=op==5?c>=0:op==6?c<=0:op==7?c>0:op==8?c<0:op==9?c==0:c!=0; return r;
  }
  if(a.type==4||b.type==4){r.i=op==9?a.i==b.i:op==10?a.i!=b.i:0;return r;}
  if(a.type==1||b.type==1){
    float x=a.type==1?a.d:(float)a.i,y=b.type==1?b.d:(float)b.i;
    switch(op){
      case 5:r.i=x>=y;break; case 6:r.i=x<=y;break; case 7:r.i=x>y;break; case 8:r.i=x<y;break; case 9:r.i=x==y;break; case 10:r.i=x!=y;break;
      case 1:r.type=1;r.d=x+y;break; case 2:r.type=1;r.d=x*y;break; case 3:r.type=1;r.d=x-y;break; case 4:r.type=1;r.d=y!=0?x/y:0;break;
      case 14:r.type=1;r.d=y!=0?x-(s32)(x/y)*y:0;break; case 15:r.i=y!=0?(s32)(x/y):0;break;
    }
    return r;
  }
  s32 x=a.i,y=b.i;
  switch(op){
    case 5:r.i=x>=y;break; case 6:r.i=x<=y;break; case 7:r.i=x>y;break; case 8:r.i=x<y;break; case 9:r.i=x==y;break; case 10:r.i=x!=y;break;
    case 1:r.i=x+y;break; case 2:r.i=x*y;break; case 3:r.i=x-y;break; case 4:r.i=y?x/y:0;break; case 11:r.i=x||y;break; case 12:r.i=x&&y;break; case 14:r.i=y?x%y:0;break; case 15:r.i=y?x/y:0;break;
  }
  return r;
}
static Value unary(int op,Value a){
  Value r=a;
  switch(op){
    case 0: if(a.type==2)r.i=-a.i; else r.d=-a.d; break;
    case 1: r.type=2;r.i=!a.i;break;
    case 2: r.type=1;r.d=(float)a.i;break;
    case 3: {char b[16];sprintf(b,"%ld",(long)a.i);r.type=3;r.s=pooled(b,"");break;}
    case 4: {char b[24];sprintf(b,"%g",(double)a.d);r.type=3;r.s=pooled(b,"");break;}
    case 5: r.type=2;r.i=(s32)a.d;break;
    case 6: r.type=2;r.i=atoi(a.s);break;
    case 7: r.type=1;r.d=(float)atof(a.s);break;
    case 8: r.type=2;r.i=(s32)(a.d+.5f);break;
    default: break;
  }
  return r;
}
static float num(Value v){return v.type==1?v.d:v.type==2?(float)v.i:0;}
static Value native_call(const FuncDef *fd,Value *a,int n);
static Value method_call(const FuncDef *fd,s32 self,Value *a,int n);
static void dispatch_binding(s32 self,int ev,Value *a,int n);
static int depth;
static Value call(int fn,Value *args,int nargs,s32 self,bool hasSelf){
  const FuncDef *fd=&gd.funcs[fn]; Value rv; rv.type=fd->type; rv.i=0;
  if(fd->kind==1)return native_call(fd,args,nargs);
  if(fd->kind==2)return method_call(fd,self,args,nargs);
  if(fd->kind==3){dispatch_binding(self,fd->native,args,nargs);return rv;}
  if(depth>=10)return rv;
  Frame f; f.sp=0; int k=0;
  for(int i=0;i<fd->nslots&&i<12;i++){f.slots[i].type=i<8?fd->ptypes[i]:0;f.slots[i].i=0;}
  for(int i=0;i<fd->nparams&&i<12;i++){
    if(i==fd->selfIndex){f.slots[i].type=4;f.slots[i].i=self;}
    else if(k<nargs)f.slots[i]=coerce(fd->ptypes[i<8?i:7],args[k++]);
  }
  for(int i=0;i<12;i++){f.regs[i].type=2;f.regs[i].i=0;}
  depth++;
  const Instr *ins=gd.ins+fd->first; s32 pc=0,count=fd->count;
  while(pc>=0&&pc<count){
    if(++vmBudget>200000)break;
    const Instr *I=&ins[pc]; s32 next=pc+1;
    switch(I->op){
      case 0: next=I->target; break;
      case 1: {Value c=rd(&I->e0,&f); if((c.i!=0)==(I->when!=0))next=I->target; break;}
      case 2: {Value r=rd(&I->e0,&f),l=rd(&I->e1,&f); if((binary(I->oper,l,r).i!=0)==(I->when!=0))next=I->target; break;}
      case 3: wr(&I->e1,rd(&I->e0,&f),&f); break;
      case 4: {Value r=rd(&I->e0,&f),l=rd(&I->e1,&f); wr(&I->e2,binary(I->oper,l,r),&f); break;}
      case 5: wr(&I->e1,unary(I->oper,rd(&I->e0,&f)),&f); break;
      case 6: {Value s=rd(&I->e0,&f),d=rd(&I->e1,&f); wr(&I->e1,binary(I->oper,d,s),&f); break;}
    }
    pc=next;
  }
  depth--;
  rv=coerce(fd->type,f.regs[0]);
  return rv;
}
void vm_run_init(void){Value none; none.i=0; depth=0; vmBudget=0; call(gd.init,&none,0,0,false);}
void vm_dispatch_name(u16 nameId,int ev,const char *label,fx time){
  int lo=0,hi=gd.nhand-1;
  while(lo<=hi){int m=(lo+hi)/2; const HandlerDef *h=&gd.hands[m]; int c=h->nameId!=nameId?h->nameId-nameId:h->eventId-ev; if(c==0){lo=m;break;} if(c<0)lo=m+1; else hi=m-1;}
  if(lo>hi)return;
  while(lo>0&&gd.hands[lo-1].nameId==nameId&&gd.hands[lo-1].eventId==ev)lo--;
  Value a[2]; int n=0;
  if(label){a[0].type=3;a[0].s=label;a[1].type=1;a[1].d=fixedToFloat(time,12);n=2;}
  for(u32 i=lo;i<gd.nhand&&gd.hands[i].nameId==nameId&&gd.hands[i].eventId==ev;i++){call(gd.hands[i].fn,a,n,0,true);}
}
static void dispatch_binding(s32 self,int ev,Value *a,int n){
  if(self<0)return; const BindDef *b=&gd.binds[self];
  vm_dispatch_name(b->nameId,ev,n>0&&a[0].type==3?a[0].s:NULL,n>1?floattof32(num(a[1])):0);
}
static Value method_call(const FuncDef *fd,s32 self,Value *a,int n){
  Value r; r.type=fd->type; r.i=0;
  if(self==-0x7fffffff)return r;
  int verb=fd->native; const u16 *list; int count; u16 single;
  if(self<0){single=~self;list=&single;count=1;} else {const BindDef *b=&gd.binds[self];list=gd.bindList+b->first;count=b->count;}
  if(verb==V_GETONE){if(count){r.type=4;r.i=~(s32)list[0];}return r;}
  for(int i=0;i<count;i++){
    Object *o=&objects[list[i]];
    switch(verb){
      case V_SETTO: case V_MOVETO: {
        if(!o->hasMotion)break;
        fx target=floattof32(num(a[0]));
        if(o->d->kind==K_CONTROLLER){o->mPlaying=0;o->mGen++;o->ctKind=verb==V_SETTO?2:1;o->ctTarget=target;o->ctPending=true;o->mLoop=0;o->mFinished=0;o->motionStarted=true;o->moving=false;}
        else if(verb==V_SETTO){o->mTime=target;o->mPlaying=0;o->mFinished=0;}
        else motion_start(o,(int)(num(a[0])*256));
        break;}
      case V_SETMOTIONSPEED: o->mSpeed=floattof32(num(a[0])); break;
      case V_MULTIPLYMOTIONSPEED: o->mSpeed=mulf32(o->mSpeed,floattof32(num(a[0]))); break;
      case V_PLAYAMBIENT: game_music(o,0); break;
      case V_PLAYACTION: game_music(o,1); break;
      case V_PLAYSPECIAL: game_music(o,2); break;
      case V_RESET: o->triggerCount=0;o->switchCount=0;o->inside=false;o->triggerPhase=0;o->pendingSwitch=-1; break;
      case V_ROTATEY: o->yaw+=(int)(num(a[0])*32768/360); break;
      case -1: vmUnknown++; break;
      case V_STOPPLAYING: case V_MULTIPLYVOLUME: case V_PLAYVICTORY: case V_ROTATEX: case V_ROTATEZ: break;
      default: game_command(o,verb);
    }
  }
  return r;
}
static Value native_call(const FuncDef *fd,Value *a,int n){
  Value r; r.type=fd->type; r.i=0;
  switch(fd->native){
    case N_STARTCUTSCENE: game_cutscene_start(); break;
    case N_STOPCUTSCENE: game_cutscene_stop(); break;
    case N_CUTSCENESAY: case N_COMMUNICATORSAY: if(n)dialogue_say(a[0].s); break;
    case N_SAY: break;  // only developer notes such as "start cut scene"
    case N_RCHIDE: playerVisible=false; break;
    case N_RCSHOW: playerVisible=true; break;
    case N_PLAYERVISIBLE: playerVisible=a[0].i!=0; break;
    case N_FREEZEENEMIES: enemiesFrozen=true; break;
    case N_UNFREEZEENEMIES: enemiesFrozen=false; break;
    case N_RCSHOWATSPAWNPOINT: {Object *o=game_find(a[0].s); if(o){vec3 p={o->x,o->y,o->z}; game_teleport(&p,n>1?(int)num(a[1]):0);} playerVisible=true; break;}
    case N_RCSETSAVEPOINT: {Object *o=game_find(a[0].s); if(o){vec3 p={o->x,o->y,o->z}; game_set_checkpoint(&p,n>1?(int)num(a[1]):0);} break;}
    case N_RCHASALLPOTIONS: {static const s16 req[5]={10,20,30,30,0}; int k=(int)num(a[0]); r.i=ps.potions>=(k>=0&&k<5?req[k]:999); break;}
    case N_RCENABLESKILL: {int k=(int)num(a[0]); if(k<0)k=0; if(k>4)k=4; if(gameLevel==3&&k==4)k=2; ps.skill|=1<<k; break;}
    case N_GETGAMETYPE: r.i=gameReplay; break;
    case N_KILLPLAYER: game_damage(9999); break;
    case N_RESPAWNPLAYER: {ps.health=ps.maxHealth; game_teleport(&checkpoint,-1); break;}
    case N_GIVEPLAYERKEYITEM: {u32 h=0; for(const char *c=a[0].s;*c;c++)h=h*31+(*c|32); for(int i=0;i<8;i++)if(!keyItems[i]||keyItems[i]==h){keyItems[i]=h;break;} break;}
    case N_PLAYERHASKEYITEM: {u32 h=0; for(const char *c=a[0].s;*c;c++)h=h*31+(*c|32); for(int i=0;i<8;i++)if(keyItems[i]==h)r.i=1; break;}
    case N_PLAYWAVSOUND: break;
    case N_RCHINTMOTION: hintMotion=(int)num(a[0]); break;
    default: vmUnknown++; break;
  }
  return r;
}
