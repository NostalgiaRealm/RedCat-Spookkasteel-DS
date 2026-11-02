// Objects, motions, enemies, projectiles and level flow.
#include "game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
GameData gd; Object *objects; PlayerState ps={10,10,3,0,0,0,0,0,0}; fx gameTime;
bool cutscene,enemiesFrozen,playerVisible=true,levelDone,cutsceneSkipping; int nextLevel=-1; vec3 checkpoint;
int gameLevel; bool gameReplay; int potionTotal;
static int checkpointOrient;
int gameDifficulty=1,hintMotion;
static fx deadTimer,respawnTimer,hitTimer,hurtSoundUntil,protection,damageFrac,chargeAge,attackCooldown,attackUntil,pendingAt,pendingCharge,lastAttacked,stepPhase; static bool pendingShot; static int stepSide;
static int playerClip; static fx playerAnim,playerRate=4096; static bool playerLoop=true;
static int redcat; static bool wantSave;
static Object *target; static bool targetLocked; static fx targetRefresh;
static struct {int idle,fw,bw,sl,sr,jump1,jump2,fall,shoot,death,hit,respawn;} pc;
static int musicMode; static u32 frameNo;
static bool pendingStop; static const Object *camObj; static fx camStart; static int soundSerial;
static const Object *maxActor,*maxModel;
typedef struct {bool active,hostile,homing; u8 kind,sprite,size; vec3 pos; s32 dir[3]; fx speed,accel,top,life,damage,gravity,fall; u16 color;} Shot;
#define MAX_SHOTS 16
static Shot shots[MAX_SHOTS];
#define WALKABLE ((s32)(.65*(1<<30)))
static u8 liquidModels[8]; static int nLiquid;

static u16 *motionObjs; static int nMotionObjs;
// Everything of the old level goes before anything of the new one is read: together they would not fit.
void game_unload(void){free(motionObjs);motionObjs=NULL;nMotionObjs=0;free(objects);objects=NULL;free(gd.base);gd.base=NULL;}
bool game_load(int index){
  char name[32]; sprintf(name,"nitro:/l%d.gam",index);
  FILE *f=fopen(name,"rb"); if(!f)return false;
  fseek(f,0,SEEK_END); size_t n=ftell(f); fseek(f,0,SEEK_SET);
  u8 *b=malloc(n); if(!b){fclose(f);return false;} fread(b,1,n,f); fclose(f); DC_FlushAll();
  const s32 *h=(const s32*)b; const u32 *o=(const u32*)(h+16);
  gd.base=b; gd.nobj=h[0];gd.nstr=h[1];gd.nbind=h[2];gd.nhand=h[3];gd.nfunc=h[4];gd.nins=h[5];gd.nglob=h[6];gd.nmotion=h[7];gd.nmodel=h[8];gd.init=h[9];
  gd.gn[0]=h[10];gd.gn[1]=h[11];gd.gn[2]=h[12];gd.glo[0]=h[13];gd.glo[1]=h[14];gd.glo[2]=h[15];
  gd.objs=(const ObjDef*)(b+o[0]);gd.strOff=(const u32*)(b+o[1]);gd.strData=(const char*)(b+o[2]);gd.binds=(const BindDef*)(b+o[3]);gd.bindList=(const u16*)(b+o[4]);
  gd.hands=(const HandlerDef*)(b+o[5]);gd.funcs=(const FuncDef*)(b+o[6]);gd.ins=(const Instr*)(b+o[7]);gd.globs=b+o[8];gd.motions=(const MotionDef*)(b+o[9]);gd.motionData=b+o[10];
  gd.models=(const ModelDef*)(b+o[11]);gd.clips=(const ClipDef*)(b+o[12]);gd.modelLists=(const u32*)(b+o[13]);gd.frames=b+o[14];gd.grid=(const u16*)(b+o[15]);
  gd.cams=(const CamDef*)(b+o[16]);gd.camPoints=(const s32*)(b+o[17]);gd.wind=(const s32*)(b+o[18]);gd.waypoints=(const float*)(b+o[19]);gd.stats=(const EnemyStat*)(b+o[20]);gd.wps=(const WayPoint*)(b+o[21]);gd.meta=(const LevelMeta*)(b+o[22]);
  objects=calloc(gd.nobj,sizeof(Object));
  audio_level(index);
  return objects!=NULL;
}
Object *game_find(const char *name){
  Object *any=NULL;
  for(u32 i=0;i<gd.nobj;i++){const ObjDef *d=&gd.objs[i];
    for(int k=0;k<3;k++) if(d->name[k]!=0xFFFF&&!strcasecmp(str_at(d->name[k]),name)){
      if(d->cls==C_ENDPOINT)return &objects[i]; if(!any)any=&objects[i];}
  }
  return any;
}
void game_event(Object *o,int ev,const char *label,fx time){
  const ObjDef *d=o->d; u16 seen[3]; int ns=0;
  for(int k=0;k<3;k++){u16 id=d->name[k]; if(id==0xFFFF)continue; bool dup=false; for(int j=0;j<ns;j++)if(seen[j]==id)dup=true; if(dup)continue; seen[ns++]=id;
    vm_dispatch_name(id,ev,label,time);}
}
// ---------- motions ----------
static void motion_pose(const Object *o,fx t,ModelState *ms){
  const MotionDef *m=&gd.motions[o->d->motion];
  fx tr[3]={0,0,0};
  const u8 *data=gd.motionData;
  if(m->nT){const s32 *k=(const s32*)(data+m->oT); int i=0; while(i+1<m->nT&&k[(i+1)*4]<=t)i++;
    if(i+1>=m->nT||t<=k[i*4]){tr[0]=k[i*4+1];tr[1]=k[i*4+2];tr[2]=k[i*4+3];}
    else {s32 f=divf32(t-k[i*4],k[(i+1)*4]-k[i*4]); for(int c=0;c<3;c++)tr[c]=k[i*4+1+c]+mulf32(k[(i+1)*4+1+c]-k[i*4+1+c],f);}
  }
  ms->rotated=false;
  s32 q[4]={0,0,0,16384};
  if(m->nR){const u8 *rk=data+m->oR; int i=0;
    #define RT(n) (*(const s32*)(rk+(n)*12))
    #define RQ(n) ((const s16*)(rk+(n)*12+4))
    while(i+1<m->nR&&RT(i+1)<=t)i++;
    const s16 *a=RQ(i);
    if(i+1>=m->nR||t<=RT(i)){for(int c=0;c<4;c++)q[c]=a[c];}
    else {const s16 *b=RQ(i+1); s32 f=divf32(t-RT(i),RT(i+1)-RT(i)); int dot=0; for(int c=0;c<4;c++)dot+=(a[c]*b[c])>>14; int sg=dot<0?-1:1;
      for(int c=0;c<4;c++)q[c]=a[c]+((sg*b[c]-a[c])*f>>12);
      float nl=0; for(int c=0;c<4;c++)nl+=(float)q[c]*q[c]; nl=16384.0f/sqrtf(nl); for(int c=0;c<4;c++)q[c]=(s32)(q[c]*nl);}
  }
  if(q[0]||q[1]||q[2]||(q[3]!=16384&&q[3]!=-16384)){
    s32 x=q[0],y=q[1],z=q[2],w=q[3]; // 2.14
    #define M2(a,b) (((a)*(b))>>15)  /* 2*a*b in 4.12 */
    s16 *r=ms->rot; int xx=M2(x,x),yy=M2(y,y),zz=M2(z,z),xy=M2(x,y),xz=M2(x,z),yz=M2(y,z),wx=M2(w,x),wy=M2(w,y),wz=M2(w,z);
    r[0]=4096-yy-zz;r[1]=xy-wz;r[2]=xz+wy;r[3]=xy+wz;r[4]=4096-xx-zz;r[5]=yz-wx;r[6]=xz-wy;r[7]=yz+wx;r[8]=4096-xx-yy;
    ms->rotated=true;
  }
  ms->pivot[0]=m->origin[0];ms->pivot[1]=m->origin[1];ms->pivot[2]=m->origin[2];
  ms->t[0]=tr[0];ms->t[1]=tr[1];ms->t[2]=tr[2];
}
static inline void motion_apply(const Object *o){if(o->d->brush>0)motion_pose(o,o->mTime,&modelState[o->d->brush]);}
static inline s32 mul30(s32 n,fx v){return (s32)(((s64)n*v)>>30);}
// Where a point on the mover ends up when the mover goes from pose a to pose b.
static void carry(const ModelState *a,const ModelState *b,vec3 *p){
  fx x=p->x-a->pivot[0]-a->t[0],y=p->y-a->pivot[1]-a->t[1],z=p->z-a->pivot[2]-a->t[2];
  if(a->rotated){const s16 *r=a->rot; fx lx=(s32)(((s64)r[0]*x+(s64)r[3]*y+(s64)r[6]*z)>>12),ly=(s32)(((s64)r[1]*x+(s64)r[4]*y+(s64)r[7]*z)>>12),lz=(s32)(((s64)r[2]*x+(s64)r[5]*y+(s64)r[8]*z)>>12); x=lx;y=ly;z=lz;}
  if(b->rotated){const s16 *r=b->rot; fx wx=(s32)(((s64)r[0]*x+(s64)r[1]*y+(s64)r[2]*z)>>12),wy=(s32)(((s64)r[3]*x+(s64)r[4]*y+(s64)r[5]*z)>>12),wz=(s32)(((s64)r[6]*x+(s64)r[7]*y+(s64)r[8]*z)>>12); x=wx;y=wy;z=wz;}
  p->x=x+b->pivot[0]+b->t[0];p->y=y+b->pivot[1]+b->t[1];p->z=z+b->pivot[2]+b->t[2];
}
static bool near_player(const ModelState *a,const ModelState *b,int brush){
  const BrushModel *bm=&level.models[brush]; const fx pp[3]={player.pos.x,player.pos.y,player.pos.z};
  bool rot=a->rotated||b->rotated;
  int r=0; if(rot)for(int k=0;k<3;k++){int lo=abs(bm->min[k]-(a->pivot[k]>>12)),hi=abs(bm->max[k]-(a->pivot[k]>>12)); r+=lo>hi?lo:hi;}
  for(int k=0;k<3;k++){
    int v=pp[k]>>12,lo,hi;
    if(rot){lo=(a->pivot[k]>>12)-r+((a->t[k]<b->t[k]?a->t[k]:b->t[k])>>12);hi=(a->pivot[k]>>12)+r+((a->t[k]>b->t[k]?a->t[k]:b->t[k])>>12);}
    else {lo=bm->min[k]+((a->t[k]<b->t[k]?a->t[k]:b->t[k])>>12);hi=bm->max[k]+((a->t[k]>b->t[k]?a->t[k]:b->t[k])>>12);}
    if(v<lo-90||v>hi+90)return false;
  }
  return true;
}
// A solid mover carries RedCat when he stands on it and pushes him aside; if that fails it waits. As the main port's guard.
static bool motion_try(const Object *o,fx t){
  int b=o->d->brush; if(b<=0||o->d->motion<0)return true;
  ModelState *ms=&modelState[b]; ModelState old=*ms,next=old; motion_pose(o,t,&next);
  if(!old.solid||cutsceneSkipping||deadTimer>0||!near_player(&old,&next,b)){*ms=next;return true;}
  vec3 p=player.pos; Trace tr;
  if(player.grounded){
    vec3 dn={p.x,p.y-FX(2),p.z}; collide_trace_only(&tr,&p,&dn,PLAYER_MINS,PLAYER_MAXS,b);
    if(tr.fraction<4096&&!tr.startSolid&&tr.normal[1]>WALKABLE){
      vec3 c=p; carry(&old,&next,&c); collide_trace_skip(&tr,&p,&c,PLAYER_MINS,PLAYER_MAXS,b);
      if(tr.fraction<4096)return false;
      p=c;
    }
  }
  *ms=next;
  for(int i=0;;i++){
    collide_trace_only(&tr,&p,&p,PLAYER_MINS,PLAYER_MAXS,b); if(!tr.startSolid)break;
    if(i==6){*ms=old;return false;}
    vec3 q={p.x+mul30(tr.penNormal[0],tr.penDist),p.y+mul30(tr.penNormal[1],tr.penDist),p.z+mul30(tr.penNormal[2],tr.penDist)};
    Trace c; collide_trace_skip(&c,&p,&q,PLAYER_MINS,PLAYER_MAXS,b);
    if(c.fraction<4096){*ms=old;return false;}
    p=q;
  }
  player.pos=p;
  return true;
}
static void motion_play(Object *o,fx from,fx to,int loop,fx speed){
  const MotionDef *m=&gd.motions[o->d->motion]; fx end=m->playEnd;
  if(from<m->start)from=m->start; if(from>end)from=end; if(to<m->start)to=m->start; if(to>end)to=end;
  o->mFrom=from;o->mTo=to;o->mTime=from;o->mSpeed=abs(speed);o->mLoop=loop;o->mLoopFrom=from;o->mLoopTo=to;o->mPlaying=1;o->mFinished=0;o->mIncludeStart=1;o->mGen++;
}
void motion_start(Object *o,int to256){
  if(!o->hasMotion)return; const MotionDef *m=&gd.motions[o->d->motion];
  int mode=o->d->p[0]; int loop=o->d->kind==K_CONTROLLER?(mode==2?2:mode==1?1:0):0;
  fx speed=o->mSpeed?o->mSpeed:4096;
  if(to256>=0){motion_play(o,o->mTime,to256<<4,0,speed);o->moving=true;o->motionStarted=true;return;}
  fx end=loop?m->end:m->playEnd;
  fx from=o->mFinished&&loop?(o->d->p[1]<<4):(o->mTime<end?o->mTime:end);
  motion_play(o,from,end,loop,speed); o->mLoopFrom=m->start;o->mLoopTo=end; o->moving=true;o->motionStarted=true;
}
static void motion_complete(Object *o);
static void motion_dispatch(Object *o,fx from,fx to,u16 gen){
  const MotionDef *m=&gd.motions[o->d->motion]; if(!m->nE){o->mTime=to;o->mIncludeStart=0;return;}
  const u8 *e=gd.motionData+m->oE; bool forward=o->mTo>=o->mFrom; bool inc=o->mIncludeStart; o->mIncludeStart=0;
  for(int n=0;n<m->nE;n++){int i=forward?n:m->nE-1-n; fx t=*(const s32*)(e+i*8); u16 s=*(const u16*)(e+i*8+4);
    bool crossed=forward?(t>from&&t<=to):(t<from&&t>=to);
    if(!crossed&&!(inc&&t==from))continue;
    o->mTime=t; game_event(o,EV_MOTIONCOMMAND,str_at(s),t);
    if(o->mGen!=gen||!o->mPlaying)return;
  }
  o->mTime=to;
}
// MoveTo/SetTo replace a controller's RepeatMode: labels fire ascending, and arriving leaves it enabled.
static void controller_target(Object *o,fx dt){
  if(!o->enabled){o->mPlaying=0;o->moving=false;return;}
  const MotionDef *m=&gd.motions[o->d->motion];
  if(o->ctPending){
    o->ctPending=false;o->ctMask=0;
    if(o->ctTarget<m->start)o->ctTarget=m->start; if(o->ctTarget>m->end)o->ctTarget=m->end;
    motion_play(o,o->mTime,o->ctTarget,0,o->mSpeed?o->mSpeed:4096);o->mTo=o->ctTarget;o->moving=true;
  } else if(!o->mPlaying&&!o->mFinished)o->mPlaying=1;
  if(!o->mPlaying)return;
  fx from=o->mTime,to=o->ctTarget,low=from,high=to; bool passed=true; u16 gen=o->mGen;
  if(o->ctKind==1){
    int dir=o->mTo>=o->mFrom?1:-1; fx proposed=from+dir*mulf32(dt,o->mSpeed);
    passed=dir*(proposed-o->ctTarget)>0; to=passed?o->ctTarget:proposed;
    low=from<to?from:to;high=from>to?from:to;
  }
  const u8 *e=gd.motionData+m->oE;
  for(int i=0;i<m->nE;i++){fx t=*(const s32*)(e+i*8); if(t<low||t>=high||(i<32&&(o->ctMask>>i)&1))continue;
    if(i<32)o->ctMask|=1u<<i;
    game_event(o,EV_MOTIONCOMMAND,str_at(*(const u16*)(e+i*8+4)),t);
    if(o->mGen!=gen||!o->mPlaying)return;
  }
  if(from!=to&&!motion_try(o,to))return;
  if(o->mGen!=gen||!o->mPlaying)return;
  o->mTime=to;o->mIncludeStart=0;
  if(passed){o->mPlaying=0;o->mFinished=1;motion_complete(o);}
}
void motion_update(fx dt){
  for(int n=0;n<nMotionObjs;n++){Object *o=&objects[motionObjs[n]];
    if(o->d->kind==K_BUTTON){
      if(!o->enabled)continue;
      int ph=o->buttonPhase;
      if(ph==1||ph==5){ if(o->pendingSwitch>=0){o->pendingSwitch=-1; int mx=o->d->p[0]; if(!(mx>0&&o->switchCount>=mx)){o->switchCount++;o->buttonPhase=ph==1?2:6;o->switchedOn=ph==1;}} }
      else if(ph==2||ph==6){ if(ph==2)game_secret(o); {int bt=o->d->p[4]; if(bt>=1&&bt<=3){vec3 c={o->x,o->y,o->z}; audio_sfx_at(S_BTN_PUSH+bt-1,&c,127);}} game_event(o,ph==2?EV_SWITCHONCOMMAND:EV_SWITCHOFFCOMMAND,NULL,0); const MotionDef *m=&gd.motions[o->d->motion];
        motion_play(o,o->mTime,ph==2?m->playEnd:m->start,0,o->mSpeed?o->mSpeed:4096); o->buttonPhase=ph==2?3:7; }
      else if(ph==4){game_event(o,EV_AFTERSWITCHONCOMMAND,NULL,0);o->buttonPhase=5;}
      else if(ph==8){o->buttonPhase=1;o->switchedOn=false;game_event(o,EV_AFTERSWITCHOFFCOMMAND,NULL,0);}
    }
    if(o->ctKind){fx before=o->mTime; controller_target(o,dt); if(o->mTime!=before)motion_apply(o); continue;}
    if(!o->mPlaying)continue;
    u16 gen=o->mGen; fx rem=mulf32(dt,o->mSpeed);
    {int dir=o->mTo>=o->mFrom?1:-1; fx dist=dir*(o->mTo-o->mTime); if(dist<0)dist=0; if(!motion_try(o,o->mTime+dir*(rem<dist?rem:dist)))continue;}
    for(int guard=0;guard<4;guard++){
      int dir=o->mTo>=o->mFrom?1:-1; fx dist=dir*(o->mTo-o->mTime); if(dist<0)dist=0; fx adv=rem<dist?rem:dist; fx next=o->mTime+dir*adv;
      motion_dispatch(o,o->mTime,next,gen); if(o->mGen!=gen||!o->mPlaying)break;
      rem-=adv; if(adv<dist)break;
      o->mTime=o->mTo;
      if(!o->mLoop||o->mLoopFrom==o->mLoopTo){o->mPlaying=0;o->mFinished=1;motion_complete(o);break;}
      if(o->mLoop==2){fx a=o->mFrom;o->mFrom=o->mTo;o->mTo=a;o->mIncludeStart=0;}
      else {o->mTime=o->mLoopFrom;o->mFrom=o->mLoopFrom;o->mTo=o->mLoopTo;o->mIncludeStart=1;}
      if(rem<=0)break;
    }
    motion_apply(o);
  }
}
static void door_done(Object *o);
static void motion_complete(Object *o){
  o->moving=false;
  if(o->d->kind==K_CONTROLLER&&!o->ctKind)o->enabled=false;
  if(o->d->kind==K_DOOR)door_done(o);
  if(o->d->kind==K_BUTTON)o->buttonPhase=o->buttonPhase==7?8:4;
}
// ---------- cameras and cutscenes ----------
void game_activate_camera(Object *o){
  int ci=o->d->p[5]; if(!ci)return;
  if(gd.cams[ci-1].mode==0){camObj=NULL;return;}
  if(camObj==o)return;   // reactivating the running view does nothing
  camObj=o;camStart=gameTime;
}
static inline bool cameraBusy(void){return camObj&&gd.cams[camObj->d->p[5]-1].mode>=2;}
const CamDef *game_camera(fx *elapsed){
  if(!camObj)return NULL;
  *elapsed=gameTime-camStart; return &gd.cams[camObj->d->p[5]-1];
}
bool game_control_locked(void){
  if(cutscene||respawnTimer>0)return true;
  if(!camObj)return false;
  int mode=gd.cams[camObj->d->p[5]-1].mode; return mode>=2&&mode<=4;
}
static void cutscene_finish(void){cutscene=false;pendingStop=false;camObj=NULL;}
void game_cutscene_start(void){pendingStop=false;cutscene=true;}
void game_cutscene_stop(void){
  if(!cutsceneSkipping&&dialogue_busy())pendingStop=true; else cutscene_finish();
}
static bool motion_busy(void){
  for(int n=0;n<nMotionObjs;n++){const Object *o=&objects[motionObjs[n]];
    if(o->d->kind==K_BUTTON){ if(o->enabled&&((o->buttonPhase!=1&&o->buttonPhase!=5)||o->pendingSwitch>=0))return true; }
    else if(o->mPlaying||(o->enabled&&o->ctKind&&o->ctPending))return true;
  }
  return false;
}
// Skipping completes the original timeline in order: skills, doors, teleports and StopCutScene all still run.
void game_skip_cutscene(void){
  if(!cutscene)return;
  dialogue_reset(); fairy_clear();
  cutsceneSkipping=true;
  if(pendingStop)cutscene_finish();
  for(int i=0;cutscene&&i<6000&&motion_busy();i++){gameTime+=FX(.05);vmBudget=0;motion_update(FX(.05));}
  cutsceneSkipping=false;
  fairy_clear(); dialogue_reset(); cutscene_finish();
}
// ---------- commands ----------
static bool contains(const Object *o,const vec3 *p,fx pad);
void game_set_door(Object *o,bool open){
  if(o->open==open)return;
  vec3 pp=player.pos;
  if(!open&&contains(o,&pp,0)){o->closeAt=gameTime+FX(.25);return;}
  if(open&&o->locked){game_event(o,EV_DOORISLOCKEDCOMMAND,NULL,0); if(o->d->p[4]==1)audio_sfx(S_ERROR); return;}
  o->open=open;
  if(open)game_secret(o);
  soundSerial=0;
  game_event(o,open?EV_DOORBEFOREOPENCOMMAND:EV_DOORBEFORECLOSECOMMAND,NULL,0);
  if(o->open!=open)return;
  o->closeAt=0;
  // Only real door panels sound, and a script's own cue replaces the automatic one (door-audio.js).
  if(!soundSerial&&o->d->p[3]&&o->visible&&!o->collected){vec3 c={o->x,o->y,o->z}; audio_sfx_at(S_DOOR_NORMAL+o->d->p[3]-1,&c,127);}
  if(o->hasMotion){const MotionDef *m=&gd.motions[o->d->motion]; motion_play(o,o->mTime,open?m->end:m->start,0,o->mSpeed?o->mSpeed:4096);o->moving=true;}
  else door_done(o);
}
static void door_done(Object *o){
  bool open=o->open;
  game_event(o,open?EV_DOORAFTEROPENCOMMAND:EV_DOORAFTERCLOSECOMMAND,NULL,0);
  if(open&&o->open&&!o->moving){int w=o->d->p[0]; o->closeAt=w>0?gameTime+(w<<8):0;}
}
static void switch_button(Object *o,bool on,bool explicit_){
  if((!o->enabled&&!explicit_)||o->switchedOn==on)return;
  if(o->hasMotion){o->pendingSwitch=on;return;}
  int mx=o->d->p[0]; if(mx>0&&o->switchCount>=mx)return;
  o->switchedOn=on;o->switchCount++;
  if(on)game_secret(o);
  {int bt=o->d->p[4]; if(bt>=1&&bt<=3){vec3 c={o->x,o->y,o->z}; audio_sfx_at(S_BTN_PUSH+bt-1,&c,127);}}
  game_event(o,on?EV_SWITCHONCOMMAND:EV_SWITCHOFFCOMMAND,NULL,0);
  game_event(o,on?EV_AFTERSWITCHONCOMMAND:EV_AFTERSWITCHOFFCOMMAND,NULL,0);
}
void game_trigger(Object *o,bool explicit_,bool contact){
  const s16 *p=o->d->p; int mx=p[0];
  if((!explicit_&&!o->enabled)||(mx>0&&o->triggerCount>=mx))return;
  o->triggerCount++;
  if(o->triggerCount<p[1])return;
  if(contact)o->triggerPhase=1;
  game_secret(o);
  if(o->d->cls==C_CAMERA)game_activate_camera(o);
  game_event(o,EV_COMMANDONENTER,NULL,0);
  if(p[3]>=0){levelDone=true;nextLevel=p[3];}
  if(explicit_)game_event(o,EV_COMMANDONLEAVE,NULL,0);
}
static void update_model_state(int brush);
void game_kill(Object *o){
  if(o->d->kind==K_ENEMY){ if(o->health>0)enemy_defeat(o); return; }
  if(o->d->kind==K_ACTOR){game_destroy(o);return;}
  if(o->health<=0)return;
  o->health=0;o->enabled=false;o->collected=true;
  game_event(o,EV_AFTERDESTROYCOMMAND,NULL,0);
}
void game_sound(Object *o){soundSerial++; if(o->d->p[0]>=0&&!o->d->p[1]){vec3 c={o->x,o->y,o->z}; if(o->d->p[4])audio_level_at(o->d->p[0],&c,127); else audio_level_sfx(o->d->p[0]);}}
void game_command(Object *o,int verb){
  const int kind=o->d->kind,cls=o->d->cls;
  switch(verb){
    case V_OPEN: case V_CLOSE: game_set_door(o,verb==V_OPEN); break;
    case V_LOCK: case V_UNLOCK: {
      bool lock=verb==V_LOCK,changed=o->locked!=lock; o->locked=lock;
      if(changed&&kind==K_DOOR&&o->d->p[5]>=1&&o->d->p[5]<=3){vec3 c={o->x,o->y,o->z}; audio_sfx_at(S_LOCK1+(o->d->p[5]-1)*2+(lock?0:1),&c,127);}
      break;}
    case V_ENABLE: case V_DISABLE: {
      bool en=verb==V_ENABLE,changed=o->enabled!=en;
      if(kind==K_FAIRY&&en)for(u32 i=0;i<gd.nobj;i++){Object *f=&objects[i]; if(f!=o&&f->d->kind==K_FAIRY&&f->enabled)f->enabled=false;}
      o->enabled=en;
      if(kind==K_FAIRY&&changed)fairy_enable(o,en);
      if(kind==K_CONTROLLER&&o->hasMotion){
        if(o->ctKind){ if(!en){o->mPlaying=0;o->moving=false;} }
        else if(en&&changed){ if(!o->mFinished&&o->motionStarted)o->mPlaying=1; else motion_start(o,-1); }
        else if(!en)o->mPlaying=0;
      }
      if(cls==C_SOUND&&en)game_sound(o);
      if(en&&cls==C_CAMERA&&o->d->p[7]){int mx=o->d->p[0]; if(!(mx>0&&o->triggerCount>=mx)){o->triggerCount++;game_activate_camera(o);}}
      if(cls==C_FLASH&&en)fx_flash(o);
      break;}
    case V_SHOW: case V_HIDE: if(verb==V_SHOW&&cls==C_TELEPORTER){fx_teleporter(o);break;} o->visible=verb==V_SHOW; break;
    case V_SWITCHON: switch_button(o,true,true); break;
    case V_SWITCHOFF: switch_button(o,false,true); break;
    case V_SWITCH: switch_button(o,!o->switchedOn,true); break;
    case V_TRIGGER: game_trigger(o,true,false); break;
    case V_DESTROY: game_kill(o); break;
  }
  if(o->d->brush>0)update_model_state(o->d->brush);
}
void game_teleport(const vec3 *p,int orientation){
  if(orientation<0)orientation=checkpointOrient;
  player.pos=*p;player.vel=(vec3){0,0,0};player.grounded=false;player.launch[0]=player.launch[1]=0;
  player.yaw=(-orientation*32768/12)&0x7fff; camera_set_yaw(player.yaw);
}
void game_set_checkpoint(const vec3 *p,int orientation){checkpoint=*p;checkpointOrient=orientation;wantSave=true;}
void game_save_extra(s32 *extra){*extra=(checkpointOrient&255)|(cutscene?256:0)|(playerVisible?512:0)|(enemiesFrozen?1024:0);}
void game_load_extra(s32 extra){checkpointOrient=(s8)(extra&255);cutscene=extra&256;playerVisible=extra&512;enemiesFrozen=extra&1024;wantSave=false;camera_set_yaw(player.yaw);}
void game_music(Object *o,int mode){int t=o->d->p[mode]; if(t>=0)audio_music(t);}
static void update_model_state(int brush){
  bool visible=true,solid=true,only=true; int count=0; Object *door=NULL;
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->brush!=brush||o->d->kind==K_PICKUP||o->d->kind==K_ACTOR||o->d->kind>=K_SPOUT)continue;
    count++; if(o->d->kind!=K_TRIGGER)only=false; if(!door&&o->d->kind==K_DOOR)door=o;
    bool present=o->visible&&!o->collected&&o->health>0; visible=visible&&present; solid=solid&&present&&!(o->d->flags&32);}
  only=only&&count>0;
  ModelState *ms=&modelState[brush];
  ms->visible=(!only||(level.models[brush].flags&1))&&visible&&!(door&&door->open&&!door->hasMotion);
  ms->solid=!only&&solid&&!(door&&door->open&&!door->hasMotion);
}
// ---------- start ----------
// The smallest world cell around a point, for visibility tests.
static u16 find_cell(fx px,fx py,fx pz){
  int x=px>>12,y=py>>12,z=pz>>12,best=0x7fffffff; u16 cell=0xFFFF;
  for(u32 c=0;c<level.h->ncells;c++){const Cell *ce=&level.cells[c]; if(x<ce->min[0]-4||x>ce->max[0]+4||y<ce->min[1]-40||y>ce->max[1]+40||z<ce->min[2]-4||z>ce->max[2]+4)continue; int size=ce->max[0]-ce->min[0]+ce->max[2]-ce->min[2]; if(size<best){best=size;cell=c;}}
  return cell;
}
void game_start(void){
  redcat=0; cutscene=false;enemiesFrozen=false;playerVisible=true;levelDone=false;nextLevel=-1;gameTime=0;deadTimer=respawnTimer=hitTimer=hurtSoundUntil=protection=damageFrac=chargeAge=attackCooldown=attackUntil=lastAttacked=stepPhase=0;pendingShot=false;hintMotion=0;
  pendingStop=false;camObj=NULL;potionTotal=0;target=NULL;targetLocked=false;targetRefresh=0;musicMode=0;wantSave=false;ps.secrets=0;ps.kills=0;
  pc.idle=model_clip(0,"idle");pc.fw=model_clip(0,"walkfw");pc.bw=model_clip(0,"walkbw");pc.sl=model_clip(0,"strafel");pc.sr=model_clip(0,"strafer");pc.jump1=model_clip(0,"jump1");pc.jump2=model_clip(0,"jump2");pc.fall=model_clip(0,"fall1");pc.shoot=model_clip(0,"shoot1");pc.death=model_clip(0,"death");pc.hit=model_clip(0,"hit");pc.respawn=model_clip(0,"re-spawn");
  playerClip=pc.idle;playerAnim=0;playerLoop=true;playerRate=4096;
  memset(shots,0,sizeof shots);
  fairy_reset(); dialogue_reset();
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; const ObjDef *d=&gd.objs[i]; o->d=d; o->x=d->x;o->y=d->y;o->z=d->z;o->yaw=d->yaw;
    o->enabled=d->flags&1;o->open=d->flags&2;o->locked=d->flags&4;o->switchedOn=d->flags&8;o->visible=true;o->health=1;o->pendingSwitch=-1;
    o->buttonPhase=o->switchedOn?5:1;o->hasMotion=d->motion>=0;o->mSpeed=4096;
    if(d->kind==K_ENEMY&&d->model>=0)enemy_init(o);
    if(d->kind==K_ACTOR){o->health=d->p[2];}
    if(d->kind==K_PICKUP&&d->p[0]==2)potionTotal++;
    if(d->kind==K_FAIRY)o->enabled=false;
    if(o->hasMotion){const MotionDef *m=&gd.motions[d->motion]; o->mTime=(d->kind==K_DOOR&&o->open)||(d->kind==K_BUTTON&&o->switchedOn)?m->end:(d->kind==K_CONTROLLER?(d->p[1]<<4):m->start); if(o->mTime<m->start)o->mTime=m->start; motion_apply(o);}
    o->cell=d->model>=0||d->kind==K_SPOUT||d->kind==K_SAVEPOINT?find_cell(d->x,d->y,d->z):0xFFFF;
  }
  free(motionObjs); motionObjs=malloc(gd.nobj*2+2); nMotionObjs=0;
  for(u32 i=0;i<gd.nobj;i++)if(objects[i].hasMotion)motionObjs[nMotionObjs++]=i;
  nLiquid=0;
  for(u32 m=1;m<level.h->nmodels;m++){update_model_state(m); if((level.models[m].flags&2)&&nLiquid<8)liquidModels[nLiquid++]=m;}
  // Items rest on what is below them, 18 units up, as the main port places them.
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; const ObjDef *d=o->d; if(d->kind!=K_PICKUP||d->model<0)continue;
    const ModelDef *m=&gd.models[d->model]; fx mn[3],mx[3]; for(int k=0;k<3;k++){mn[k]=(IFX(m->bmin[k])>>8)*d->scale;mx[k]=(IFX(m->bmax[k])>>8)*d->scale;}
    vec3 a={o->x,o->y+FX(1),o->z},b={o->x,o->y-FX(3200),o->z}; Trace t; collide_trace(&t,&a,&b,mn,mx,true);
    o->pickupY=(t.fraction<4096?t.end.y-o->y:0)+FX(18);
  }
  for(u32 i=0;i<gd.nobj;i++)if(objects[i].d->kind==K_ENEMY&&objects[i].d->model>=0)enemy_place(&objects[i]);
  fx_reset();
  audio_loops_reset();
  maxActor=maxModel=NULL; if(gameLevel==3){maxActor=game_find("max_actor");maxModel=game_find("max_model");}
  game_set_checkpoint(&(vec3){level.h->spawn[0],level.h->spawn[1],level.h->spawn[2]},level.h->yaw);
  vm_init(); vm_run_init();
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->kind==K_CONTROLLER&&o->enabled&&!o->ctKind)motion_start(o,-1); if(o->d->cls==C_SOUND&&o->enabled&&!o->d->p[1])game_sound(o);}
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->cls==C_WORLD)game_event(o,EV_ONINITCOMMAND,NULL,0);}
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->cls==C_MUSIC&&o->enabled){game_music(o,0);break;}}
  wantSave=false;
}
// ---------- geometry helpers ----------
// Same bounds and contents test as the main port's contains().
static bool contains(const Object *o,const vec3 *p,fx pad){
  int b=o->d->brush;
  if(b<=0){int dx=(o->x-p->x)>>12,dy=(o->y-p->y)>>12,dz=(o->z-p->z)>>12; int r=o->d->kind==K_TRIGGER&&o->d->p[4]>24?o->d->p[4]:24;
    if(abs(dx)>r||abs(dy)>r||abs(dz)>r)return false; return dx*dx+dy*dy+dz*dz<r*r;}
  const BrushModel *bm=&level.models[b]; const ModelState *ms=&modelState[b];
  fx mn[3]={PLAYER_MINS[0]-pad,PLAYER_MINS[1]-pad,PLAYER_MINS[2]-pad},mx[3]={PLAYER_MAXS[0]+pad,PLAYER_MAXS[1]+pad,PLAYER_MAXS[2]+pad};
  if(!ms->rotated){
    const fx q[3]={p->x-ms->t[0],p->y-ms->t[1],p->z-ms->t[2]};
    for(int k=0;k<3;k++) if(q[k]+mx[k]<IFX(bm->min[k])||q[k]+mn[k]>IFX(bm->max[k]))return false;
  }
  return collide_box_contents(b,p,mn,mx)!=0;
}
static bool within_radius(const Object *o,const vec3 *p){
  int r=o->d->p[1]; if(r<=0)return false;
  fx dx=abs(o->x-p->x),dy=abs(o->y-(p->y+FX(54.08))),dz=abs(o->z-p->z),h=IFX(r)+FX(24.96);
  return dx<h&&dy<FX(57.92)&&dz<h;
}
static bool touched(const Object *o,const vec3 *p){
  if(o->d->p[2]<=0||o->d->brush<=0)return false;
  for(int i=0;i<player.ncontacts;i++)if(player.contacts[i]==o->d->brush)return true;
  return contains(o,p,FX(.1));
}
static const fx ZERO[3]={0,0,0};
bool game_los(const vec3 *a,const vec3 *b){Trace t; collide_trace(&t,a,b,ZERO,ZERO,true); return t.fraction>=4096&&!t.startSolid;}
// The object's origin carried along by the moving brush it stands on.
static vec3 mounted(const Object *o,fx addY){
  vec3 p={o->x,o->y+addY,o->z}; int b=o->d->brush; if(b<=0)return p;
  const ModelState *ms=&modelState[b];
  if(ms->rotated){const s16 *r=ms->rot; fx x=p.x-ms->pivot[0],y=p.y-ms->pivot[1],z=p.z-ms->pivot[2];
    p.x=ms->pivot[0]+(s32)(((s64)r[0]*x+(s64)r[1]*y+(s64)r[2]*z)>>12);p.y=ms->pivot[1]+(s32)(((s64)r[3]*x+(s64)r[4]*y+(s64)r[5]*z)>>12);p.z=ms->pivot[2]+(s32)(((s64)r[6]*x+(s64)r[7]*y+(s64)r[8]*z)>>12);}
  p.x+=ms->t[0];p.y+=ms->t[1];p.z+=ms->t[2];
  return p;
}
// Disabling pauses a mounted actor's controller; hiding it or its brush removes both.
static bool attached_visible(const Object *o){
  int b=o->d->brush; if(b<=0)return true;
  if(o==maxActor&&maxModel&&maxModel->mFinished)return false;
  for(u32 i=0;i<gd.nobj;i++){const Object *c=&objects[i]; if(c->d->brush!=b)continue; int k=c->d->kind; if((k==K_CONTROLLER||k==K_DOOR||k==K_BUTTON)&&(!c->visible||c->health<=0))return false;}
  return true;
}
// ---------- player combat ----------
static void player_die(void){
  ps.health=0;deadTimer=FX(2);damageFrac=0;chargeAge=0;pendingShot=false;attackUntil=0;camObj=NULL;target=NULL;targetLocked=false;audio_charge(false);audio_sfx(S_DEATH);
  playerClip=pc.death;playerAnim=0;playerLoop=false;playerRate=FX(1.6);
}
// No damage in cutscenes, while dead or during the five seconds after a respawn; fractions add up. Every hit counts, as in the main port.
void game_damage_fx(fx amount,bool continuous){
  if(levelDone||ps.health<=0||amount<=0||deadTimer>0||respawnTimer>0||cutscene||protection>0)return;
  damageFrac+=amount; int whole=damageFrac>>12;
  if(whole){damageFrac&=4095; ps.health-=whole;}
  if(ps.health<=0){player_die();return;}
  if(gameTime>=hurtSoundUntil&&(whole||!continuous)){audio_sfx(amount>=IFX(40)?S_HURT3:amount>=IFX(10)?S_HURT2:S_HURT1);hurtSoundUntil=gameTime+FX(1.25);}
  if(!continuous&&hitTimer<=0){hitTimer=FX(.71);playerClip=pc.hit;playerAnim=0;playerLoop=false;playerRate=FX(1.5);}
}
void game_damage(int amount){
  if(amount>=9999){if(deadTimer<=0)player_die();return;}  // script KillPlayer passes every protection
  game_damage_fx(IFX(amount),false);
}
void game_secret(Object *o){
  if(!(o->d->flags&16)||o->secretFound)return;
  o->secretFound=true;ps.secrets++;audio_sfx(S_SECRET);hud_message("Geheim gevonden!");
}
// The graveyard's hedge maze can be tried again after dying, as in the main port.
void game_reset_object(Object *o){
  const ObjDef *d=o->d; if(!o)return;
  o->enabled=d->flags&1;o->visible=true;o->open=d->flags&2;o->locked=d->flags&4;o->switchedOn=d->flags&8;o->triggerCount=0;o->switchCount=0;o->inside=false;o->triggerPhase=0;o->pendingSwitch=-1;o->buttonPhase=o->switchedOn?5:1;o->closeAt=0;o->moving=false;o->motionStarted=false;o->mPlaying=0;o->mFinished=0;o->mSpeed=4096;o->ctKind=0;o->mGen++;
  if(o->hasMotion){const MotionDef *m=&gd.motions[d->motion]; o->mTime=(d->kind==K_DOOR&&o->open)||(d->kind==K_BUTTON&&o->switchedOn)?m->end:(d->kind==K_CONTROLLER?(d->p[1]<<4):m->start); if(o->mTime<m->start)o->mTime=m->start; motion_apply(o);}
  if(camObj==o)camObj=NULL;
  if(d->brush>0)update_model_state(d->brush);
}
static void graveyard_retry(void){
  if(gameLevel!=2)return;
  Object *seq=game_find("mazedoortrigger"),*green=game_find("mazedeur1_groen"),*door=game_find("mazedeur1_dm");
  if(!seq||(green&&green->enabled)||(door&&door->open)||seq->mPlaying||seq->mFinished)return;
  static const char *const NAMES[]={"heg1_mc","heg2_mc","heg3_mc","heg4_mc","heg5_mc","heg1_trigger_mc","heg3_trigger1_mc","heg3_trigger2_mc","heg2_closetrigger_mc","heg5_trigger_mc","mazedeur1_knop","heg7_mc","mazedoortrigger","mazedoorcam","mazedeur1_dm","mazedeur1_rood","mazedeur1_groen"};
  for(u32 i=0;i<sizeof NAMES/sizeof NAMES[0];i++){Object *o=game_find(NAMES[i]); if(o)game_reset_object(o);}
}
static void player_respawn(void){
  ps.health=ps.maxHealth;protection=FX(5);respawnTimer=FX(3.27);hitTimer=0;damageFrac=0;chargeAge=0;attackCooldown=0;pendingShot=false;attackUntil=0;
  memset(shots,0,sizeof shots);
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->kind==K_TRIGGER)o->inside=false; if(o->d->cls==C_WORLD)game_event(o,EV_ONPLAYERRESPAWNCOMMAND,NULL,0);}
  graveyard_retry();
  game_teleport(&checkpoint,-1); camObj=NULL;
  playerClip=pc.respawn;playerAnim=0;playerLoop=false;playerRate=4096;
}
static Shot *new_shot(void){for(int i=0;i<MAX_SHOTS;i++)if(!shots[i].active){memset(&shots[i],0,sizeof(Shot));shots[i].active=true;return &shots[i];}return NULL;}
void game_shot(int sprite,const vec3 *pos,const s32 *dir,fx speed,fx gravity,fx life,fx damage,bool homing){
  Shot *s=new_shot(); if(!s)return;
  s->hostile=true;s->sprite=sprite;s->pos=*pos;s->dir[0]=dir[0];s->dir[1]=dir[1];s->dir[2]=dir[2];s->speed=speed;s->gravity=gravity;s->life=life;s->damage=damage;s->homing=homing;s->color=0x7FFF;s->size=14;
}
// ---------- targeting: the nearest actor in front, shootable buttons after them. As EnemyTargeting in the main port. ----------
static bool target_ok(const Object *o){
  if(!o->enabled||!o->visible||o->collected||o->health<=0)return false;
  switch(o->d->kind){
    case K_ENEMY: return !o->bossHidden;
    case K_BUTTON: if(!o->d->p[3]||o->d->brush<=0||!modelState[o->d->brush].visible)return false; return !(o->d->p[0]>0&&o->switchCount>=o->d->p[0]);
    case K_ACTOR: return (o->d->p[0]&8)&&attached_visible(o);
  }
  return false;
}
static void target_point(const Object *o,vec3 *out){
  if(o->d->kind==K_ENEMY){enemy_aim(o,out);return;}
  if(o->d->kind==K_BUTTON){const BrushModel *bm=&level.models[o->d->brush]; const ModelState *ms=&modelState[o->d->brush]; *out=(vec3){IFX((bm->min[0]+bm->max[0])/2),IFX((bm->min[1]+bm->max[1])/2),IFX((bm->min[2]+bm->max[2])/2)};
    if(ms->rotated){const s16 *r=ms->rot; fx x=out->x-ms->pivot[0],y=out->y-ms->pivot[1],z=out->z-ms->pivot[2]; out->x=ms->pivot[0]+(s32)(((s64)r[0]*x+(s64)r[1]*y+(s64)r[2]*z)>>12);out->y=ms->pivot[1]+(s32)(((s64)r[3]*x+(s64)r[4]*y+(s64)r[5]*z)>>12);out->z=ms->pivot[2]+(s32)(((s64)r[6]*x+(s64)r[7]*y+(s64)r[8]*z)>>12);}
    out->x+=ms->t[0];out->y+=ms->t[1];out->z+=ms->t[2];return;}
  const ModelDef *m=&gd.models[o->d->model]; *out=mounted(o,((IFX(m->bmin[1]+m->bmax[1])>>9)*o->d->scale));
}
static bool target_eligible(const Object *o,const vec3 *eye,s32 *dist,s32 limit){
  if(!target_ok(o))return false;
  vec3 aim; target_point(o,&aim); const bool enemy=o->d->kind==K_ENEMY,button=o->d->kind==K_BUTTON;
  int dx=((enemy?o->x:aim.x)-player.pos.x)>>12,dy=((enemy?o->y:aim.y)-player.pos.y)>>12,dz=((enemy?o->z:aim.z)-player.pos.z)>>12;
  int range=button?4320:480; if(abs(dx)>range||abs(dy)>range||abs(dz)>range)return false;
  s32 d=(dx>>2)*(dx>>2)+(dy>>2)*(dy>>2)+(dz>>2)*(dz>>2); if(d>(range>>2)*(range>>2))return false;
  if(dx*-sinLerp(cam.yaw)+dz*-cosLerp(cam.yaw)<0)return false;
  if(d>=limit)return false;  // no need for a sight line to something further away
  Trace t; collide_trace(&t,eye,&aim,ZERO,ZERO,true);
  if(t.fraction<4096&&!(button&&t.model==o->d->brush))return false;
  *dist=d; return true;
}
static void target_update(fx dt,bool attack){
  if(ps.health<=0||cutscene||!(ps.skill&1)||levelDone){target=NULL;targetLocked=false;targetRefresh=0;return;}
  vec3 eye={player.pos.x,player.pos.y+FX(45),player.pos.z}; s32 d;
  if(target&&!target_eligible(target,&eye,&d,0x7fffffff)){target=NULL;targetRefresh=0;}
  if(gameTime>=targetRefresh&&!(attack&&targetLocked&&target)){
    Object *best=NULL; s32 bd=0x7fffffff; bool bestButton=true;
    for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; int k=o->d->kind; if(k!=K_ENEMY&&k!=K_BUTTON&&k!=K_ACTOR)continue; if(k==K_ACTOR&&!(o->d->p[0]&8))continue; if(k==K_BUTTON&&!o->d->p[3])continue;
      bool button=k==K_BUTTON; if(best&&!bestButton&&button)continue;
      int qx=abs(o->x-player.pos.x)>>12,qz=abs(o->z-player.pos.z)>>12; if(!button&&(qx>480||qz>480))continue;
      if(!target_eligible(o,&eye,&d,best&&bestButton&&!button?0x7fffffff:bd))continue;
      if((best&&bestButton&&!button)||d<bd){best=o;bd=d;bestButton=button;}
    }
    target=best; targetRefresh=gameTime+FX(.5);
  }
  targetLocked=target&&attack;
  if(targetLocked&&!cameraBusy()){
    vec3 aim; target_point(target,&aim); int yaw=atan2_bin(-(aim.x-player.pos.x)>>4,-(aim.z-player.pos.z)>>4);
    camera_turn_toward(yaw,dt); player.yaw=yaw;
  }
}
const Object *game_target(void){return target;}
static void player_attack(fx startFraction,fx chargeFraction){
  const fx dur=FX(1.9333/1.9);
  attackCooldown=mulf32(dur,FX(.8)-startFraction); attackUntil=gameTime+mulf32(dur,4096-startFraction);
  pendingShot=true;pendingAt=gameTime+mulf32(dur,FX(.46)-startFraction);pendingCharge=chargeFraction;
  if(target&&target->d->kind==K_ENEMY)lastAttacked=gameTime;
  playerClip=pc.shoot;playerAnim=mulf32(FX(1.9333),startFraction);playerLoop=false;playerRate=FX(1.9);
}
static void player_release(void){
  pendingShot=false;
  const int type=ps.skill&4?2:ps.skill&2?1:0; const s32 *st=&gd.meta->shots[gameDifficulty*15+type*5];
  Shot *s=new_shot(); if(!s)return;
  s32 sy=sinLerp(player.yaw),cy=cosLerp(player.yaw); vec3 from={player.pos.x-mulf32(sy,FX(10)),player.pos.y+FX(38),player.pos.z-mulf32(cy,FX(10))};
  s->pos=from;s->kind=type;s->sprite=SP_SPARK+type;s->size=type==2?20:12;s->color=0x7FFF;s->speed=IFX(st[1]);s->accel=IFX(st[2]);s->top=IFX(st[3]);s->life=st[4]<<8;
  s->damage=st[0]<<8; if(type==2){s->damage=mulf32(s->damage,pendingCharge); if(s->damage<4096)s->damage=4096;}
  if(targetLocked&&target){vec3 aim; target_point(target,&aim); int dx=(aim.x-from.x)>>6,dy=(aim.y-from.y)>>6,dz=(aim.z-from.z)>>6; int len=sqrt32((dx>>4)*(dx>>4)+(dy>>4)*(dy>>4)+(dz>>4)*(dz>>4))<<4; if(!len)len=1; s->dir[0]=divf32(dx,len);s->dir[1]=divf32(dy,len);s->dir[2]=divf32(dz,len);}
  else {s->dir[0]=-sy;s->dir[1]=0;s->dir[2]=-cy;}
  if(type==0)audio_sfx(S_SHOT); else if(type==1)audio_sfx(S_SHOT2);
}
static void player_shoot_input(fx dt,u32 keys){
  if(attackCooldown>0)attackCooldown-=dt;
  const bool canStart=attackCooldown<=0;
  if(game_control_locked()||!(ps.skill&1)||hitTimer>0){ if(chargeAge>0){chargeAge=0;audio_charge(false);} return; }
  const bool held=keys&KEY_B;
  if(!(ps.skill&4)){ if(held&&canStart&&!pendingShot)player_attack(0,4096); return; }
  if(held){
    if(chargeAge<=0){ if(canStart&&!pendingShot){chargeAge=1;audio_charge(true);} }
    else {chargeAge+=dt; if(chargeAge>FX(1.5))chargeAge=FX(1.5);}
  } else if(chargeAge>0){
    fx f=divf32(chargeAge+dt,FX(1.5)); if(f>4096)f=4096; chargeAge=0;audio_charge(false);player_attack(FX(.26),f);
  }
}
// A shot on a brush: OnHit for the objects built on it, and ShootToSwitch buttons.
static void hit_brush(int brush){
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->brush!=brush||!o->visible||o->health<=0||o->d->kind==K_PICKUP||o->d->kind==K_ACTOR)continue;
    game_event(o,EV_ONHITCOMMAND,NULL,0);
    if(o->d->kind==K_BUTTON&&o->d->p[3]&&(o->buttonPhase==1||o->buttonPhase==5))switch_button(o,!o->switchedOn,false);
  }
}
// An exploding prop: a sprite burst, a sound and 400 ms worth of its hurt cell at once.
static void blast(const vec3 *c,int size,int dmg,const Object *source){
  int radius=size*7; fx rate=dmg*FX(1.875); if(!radius||!rate)return;
  { int dx=(player.pos.x-c->x)>>12,dy=(player.pos.y+FX(29)-c->y)>>12,dz=(player.pos.z-c->z)>>12; if(abs(dx)<radius&&abs(dy)<radius&&abs(dz)<radius){int d=sqrt32(dx*dx+dy*dy+dz*dz); if(d<radius)game_damage_fx(mulf32(mulf32(rate,FX(.4)),IFX(radius-d)/radius),false);} }
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o==source||o->health<=0||!o->enabled)continue; int k=o->d->kind; if(k!=K_ENEMY&&!(k==K_ACTOR&&(o->d->p[0]&4)))continue;
    int dx=(o->x-c->x)>>12,dy=(o->y-c->y)>>12,dz=(o->z-c->z)>>12; if(abs(dx)>=radius||abs(dy)>=radius||abs(dz)>=radius)continue; int d=sqrt32(dx*dx+dy*dy+dz*dz); if(d>=radius||!game_los(c,&(vec3){o->x,o->y+FX(12),o->z}))continue;
    fx amount=mulf32(mulf32(rate,FX(.4)),IFX(radius-d)/radius);
    if(k==K_ENEMY){ if(amount>=4096)enemy_hit(o,amount); }
    else {o->propDamage+=amount; if(o->propDamage>IFX(o->health))game_destroy(o);}
  }
}
void game_destroy(Object *o){
  if(o->health<=0)return;
  o->health=0;o->enabled=false;
  const s16 ex=o->d->p[5]; vec3 c=mounted(o,FX(16)); int size=ex&127,dmg=(ex>>8)&127;
  if(ex){fx_explosion(&c,20+size);audio_sfx_at(S_EXPLOSION,&c,size*4>127?127:50+size*2);blast(&c,size,dmg,o);}
  game_event(o,EV_AFTERDESTROYCOMMAND,NULL,0);
  if(o->d->brush>0)update_model_state(o->d->brush);
}
static void shot_impact(Shot *s,const vec3 *at){
  s->active=false; fx_impact(at,s->kind); audio_sfx_at(s->kind==0?S_IMPACT:s->kind==1?S_IMPACT2:S_IMPACT3,at,127);
}
static void update_shots(fx dt){
  for(int i=0;i<MAX_SHOTS;i++){Shot *s=&shots[i]; if(!s->active)continue;
    if(s->hostile&&enemiesFrozen)continue;
    s->life-=dt; if(s->life<=0){s->active=false;continue;}
    s->speed+=mulf32(s->accel,dt); if(s->top&&s->speed>s->top)s->speed=s->top;
    fx step=mulf32(s->speed,dt);
    if(s->gravity){s->fall+=mulf32(s->gravity,dt);}
    if(s->homing){int dx=(player.pos.x-s->pos.x)>>6,dy=(player.pos.y+FX(28)-s->pos.y)>>6,dz=(player.pos.z-s->pos.z)>>6; int len=sqrt32((dx>>4)*(dx>>4)+(dy>>4)*(dy>>4)+(dz>>4)*(dz>>4))<<4; if(len){s->dir[0]+=(divf32(dx,len)-s->dir[0])>>4;s->dir[1]+=(divf32(dy,len)-s->dir[1])>>4;s->dir[2]+=(divf32(dz,len)-s->dir[2])>>4;}}
    vec3 e={s->pos.x+mulf32(s->dir[0],step),s->pos.y+mulf32(s->dir[1],step)-mulf32(s->fall,dt),s->pos.z+mulf32(s->dir[2],step)};
    Trace t; collide_trace(&t,&s->pos,&e,ZERO,ZERO,true);
    const vec3 end=t.fraction<4096?t.end:e;
    if(s->hostile){
      if(abs(end.x-player.pos.x)<FX(12.48)&&abs(end.z-player.pos.z)<FX(12.48)&&end.y>player.pos.y&&end.y<player.pos.y+FX(57.92)){game_damage_fx(s->damage,false);s->active=false;continue;}
      if(ps.health<=0)return;
    } else {
      const vec3 mid={(s->pos.x+end.x)/2,(s->pos.y+end.y)/2,(s->pos.z+end.z)/2};
      for(u32 k=0;k<gd.nobj;k++){Object *o=&objects[k]; int kind=o->d->kind;
        if(kind==K_ENEMY&&o->enabled&&o->health>0&&!o->bossHidden){
          fx mn[3],mx[3]; const ModelDef *m=&gd.models[o->d->model]; int w=((m->bmax[0]-m->bmin[0]+m->bmax[2]-m->bmin[2])*o->d->scale)>>10; if(w<14)w=14; if(w>30)w=30; int h=(m->bmax[1]*o->d->scale)>>8; if(h<24)h=24; (void)mn;(void)mx;
          bool hit=(abs(end.x-o->x)<IFX(w)&&abs(end.z-o->z)<IFX(w)&&end.y>o->y-FX(4)&&end.y<o->y+IFX(h))||(abs(mid.x-o->x)<IFX(w)&&abs(mid.z-o->z)<IFX(w)&&mid.y>o->y-FX(4)&&mid.y<o->y+IFX(h));
          if(hit){game_event(o,EV_ONHITCOMMAND,NULL,0);enemy_hit(o,s->damage);lastAttacked=gameTime;shot_impact(s,&end);break;}
        }
        else if(kind==K_ACTOR&&(o->d->p[0]&2)&&o->enabled&&o->visible&&!o->collected&&o->health>0&&o->d->model>=0){
          vec3 c=mounted(o,0); const ModelDef *m=&gd.models[o->d->model]; int r=((m->bmax[0]-m->bmin[0]+m->bmax[2]-m->bmin[2])*o->d->scale)>>10; if(r<6)r=6; if(r>64)r=64; int top=(m->bmax[1]*o->d->scale)>>8;
          if(abs(end.x-c.x)<IFX(r)&&abs(end.z-c.z)<IFX(r)&&end.y>c.y-FX(4)&&end.y<c.y+IFX(top)&&attached_visible(o)){
            game_event(o,EV_ONHITCOMMAND,NULL,0);
            if(o->d->p[0]&4){o->propDamage+=s->damage; if(o->propDamage>IFX(o->health))game_destroy(o);}
            shot_impact(s,&end);break;
          }
        }
        else if(kind==K_BUTTON&&o->d->p[3]&&o->d->brush>0&&!modelState[o->d->brush].solid){int dx=(end.x-o->x)>>12,dy=(end.y-o->y)>>12,dz=(end.z-o->z)>>12; if(abs(dx)<40&&abs(dy)<40&&abs(dz)<40&&dx*dx+dy*dy+dz*dz<40*40){hit_brush(o->d->brush);shot_impact(s,&end);break;}}
      }
      if(!s->active)continue;
    }
    if(t.fraction<4096||t.startSolid){ if(!s->hostile){ if(t.model>0)hit_brush(t.model); shot_impact(s,&end);} else s->active=false; continue; }
    s->pos=e;
  }
}
// ---------- per frame ----------
static void pickup(Object *o){
  int t=o->d->p[1]-1; if(t<0)t=0; if(t>2)t=2; static const s16 coin[3]={1,5,10},cs[3]={5,25,50},hs[3]={1,2,20},hc[3]={5,15,25};
  o->collected=true; int snd=S_POTION,score=0,card=-1; char msg[32];
  switch(o->d->p[0]){
    case 0: ps.coins+=coin[t];score=cs[t];snd=S_COIN1+t; sprintf(msg,"Geldzak gevonden! +%d",score); break;
    case 1: ps.health+=hs[t]; if(ps.health>ps.maxHealth)ps.health=ps.maxHealth; score=hc[t];snd=S_HEALTH1+t; sprintf(msg,"Je voelt je weer beter. +%d",score); break;
    case 2: if(ps.potions<100)ps.potions++; score=25;snd=S_POTION; sprintf(msg,"Toverdrank gevonden! +%d",score); break;
    case 3: if(ps.mirror<5)ps.mirror++; score=75;snd=S_MIRROR; sprintf(msg,"Spiegelstuk gevonden! +%d",score); break;
    case 4: ps.lives++; score=75;snd=S_LIFE; sprintf(msg,"Een extra leven! +%d",score); break;
    default: ps.maxHealth+=2; if(ps.maxHealth>20)ps.maxHealth=20; card=75;snd=S_HART; strcpy(msg,"Een extra hart!"); break;
  }
  ps.score+=score; if(ps.score>999999)ps.score=999999; if(o->d->flags&16)ps.secrets++;
  hud_message(msg);
  vec3 c=mounted(o,o->pickupY); fx_reward(&c,card>=0?card:score,true);
  audio_sfx(snd); game_event(o,EV_PICKUPCOMMAND,NULL,0);
}
static void player_animate(fx dt,u32 keys){
  playerAnim+=mulf32(dt,playerRate);
  if(deadTimer>0)return;
  if(respawnTimer>0){if(playerClip!=pc.respawn){playerClip=pc.respawn;playerAnim=0;playerLoop=false;playerRate=4096;} return;}
  const int fw=player.inputFwd,side=player.inputSide; const bool grounded=player.grounded;
  if(hitTimer>0){hitTimer-=dt; if(playerClip==pc.hit)return;}
  if(chargeAge>0){playerClip=pc.shoot;playerAnim=FX(1.9333*.26);playerRate=0;playerLoop=false;return;}
  if(attackUntil>gameTime&&grounded&&!fw&&!side&&!cutscene){ if(playerClip!=pc.shoot){playerClip=pc.shoot;playerLoop=false;playerRate=FX(1.9);playerAnim=FX(1.9333)-mulf32(attackUntil-gameTime,FX(1.9));} return; }
  int want; fx rate=4096; bool loop=true;
  if(hintMotion==12&&!player.jumped){want=pc.fall;rate=FX(1.5);loop=false;}
  else if(!grounded&&player.jumped){want=player.jumpedSuper?pc.jump2:pc.jump1;rate=player.jumpedSuper?FX(1.3):FX(1.4);}
  else if(!grounded&&player.vel.y<-FX(300)){want=pc.fall;rate=FX(1.5);loop=false;}
  else if(fw>0){want=pc.fw;}
  else if(fw<0){want=pc.bw;rate=FX(2.2);}
  else if(side>0){want=pc.sr;rate=FX(2.4);}
  else if(side<0){want=pc.sl;rate=FX(2.4);}
  else want=pc.idle;
  if(want!=playerClip||playerLoop!=loop){playerClip=want;playerLoop=loop;playerAnim=0;}
  playerRate=rate;
  // Footfalls, after the original's wobble clock.
  if(grounded&&(fw||side)&&!cutscene&&(keys&(KEY_UP|KEY_DOWN|KEY_L|KEY_R))){
    fx speed=fw>0?FX(4.9):FX(4.5); stepPhase+=mulf32(dt,mulf32(FX(.5*4),sqrtf32(divf32(speed,FX(3.95)))));
    if(stepPhase>=4096){stepPhase-=4096;stepSide^=1;audio_sfx(stepSide?S_STEP2:S_STEP1);}
  }
}
// Ambient while nothing is after RedCat, action in a fight, the special track for a boss.
static void update_music(bool threat,bool boss){
  int mode=boss?2:threat?1:0; if(mode==musicMode||(cutscene))return;
  musicMode=mode;
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->cls==C_MUSIC&&o->enabled){game_music(o,mode);return;}}
}
static bool segment_hits_player(const vec3 *a,const vec3 *b){
  float lo=0,hi=1; const fx p[3]={player.pos.x,player.pos.y,player.pos.z},s[3]={a->x,a->y,a->z},e[3]={b->x,b->y,b->z};
  for(int i=0;i<3;i++){float mn=fixedToFloat(p[i]+PLAYER_MINS[i],12),mx=fixedToFloat(p[i]+PLAYER_MAXS[i],12),o=fixedToFloat(s[i],12),d=fixedToFloat(e[i]-s[i],12);
    if(d>-.001f&&d<.001f){ if(o<mn||o>mx)return false; continue; }
    float t0=(mn-o)/d,t1=(mx-o)/d; if(t0>t1){float x=t0;t0=t1;t1=x;} if(t0>lo)lo=t0; if(t1<hi)hi=t1; if(lo>hi)return false;}
  return true;
}
void game_update(fx dt,u32 keys,u32 down){
  gameTime+=dt; vmBudget=0; frameNo++;
  player.env[0]=player.env[1]=player.env[2]=0;
  motion_update(dt);
  dialogue_update(dt);
  fairy_update(dt);
  fx_update(dt);
  if(camObj){const CamDef *c=&gd.cams[camObj->d->p[5]-1]; if((c->mode==4&&!c->duration)||(c->duration>0&&gameTime-camStart>=c->duration))camObj=NULL;}
  if(pendingStop&&!dialogue_busy())cutscene_finish();
  player_animate(dt,keys);
  audio_loops();
  if(protection>0)protection-=dt;
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->kind==K_ENEMY&&o->health<=0&&o->d->model>=0){o->animTime+=dt;enemy_dying(o);}}
  if(wantSave&&!cutscene){wantSave=false;save_write();hud_message("Bewaarpunt bereikt.");}
  if(deadTimer>0){
    deadTimer-=dt;
    if(deadTimer<=0){
      if(ps.lives>0){ps.lives--;player_respawn();} else {levelDone=true;nextLevel=-2;}
    }
    return;
  }
  if(respawnTimer>0){respawnTimer-=dt;return;}
  if(cutscene){if(chargeAge>0){chargeAge=0;audio_charge(false);}pendingShot=false;attackUntil=0;target=NULL;targetLocked=false;return;}
  vec3 pp=player.pos;
  // Liquids: ooze and instant death volumes.
  {u32 c=collide_box_contents(0,&pp,PLAYER_MINS,PLAYER_MAXS); for(int i=0;i<nLiquid;i++)c|=collide_box_contents(liquidModels[i],&pp,PLAYER_MINS,PLAYER_MAXS);
   fx rate=(c&0x200?FX(3):0)+(c&0x400?FX(200):0); if(rate)game_damage_fx(mulf32(rate,dt),true);}
  if(player.landed>FX(258*.05)){vec3 f=pp; audio_sfx_at(S_LAND,&f,player.landed>=FX(516)?127:(player.landed>>12)*127/516);}
  if(player.landed>FX(516))game_damage_fx(divf32(player.landed-FX(516),FX(516)),false);
  if(ps.health<=0)return;
  // Shooting: the target locks while the button is held and RedCat turns to it.
  player_shoot_input(dt,keys);
  target_update(dt,(keys&KEY_B)||attackUntil>gameTime);
  if(pendingShot&&gameTime>pendingAt&&!game_control_locked())player_release();
  update_shots(dt);
  if(ps.health<=0)return;
  for(int i=0;i<player.ncontacts;i++){int b=player.contacts[i];
    for(u32 k=0;k<gd.nobj;k++){Object *o=&objects[k]; if(o->d->brush==b&&o->d->kind==K_CONTROLLER&&o->visible&&o->health>0)game_event(o,EV_ONTOUCHCOMMAND,NULL,0);}}
  fx wind[3]={0,0,0}; bool threat=gameTime-lastAttacked<FX(2)&&lastAttacked>0,boss=false;
  for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; const int kind=o->d->kind; if(!kind||!o->enabled||o->collected||o->health<=0)continue;
    switch(kind){
      case K_PICKUP: {
        vec3 c=mounted(o,o->pickupY); const ModelDef *m=&gd.models[o->d->model]; int s=o->d->scale;
        fx ax0=c.x+(IFX(m->bmin[0])>>8)*s-FX(1),ax1=c.x+(IFX(m->bmax[0])>>8)*s+FX(1),ay0=c.y+(IFX(m->bmin[1])>>8)*s-FX(1),ay1=c.y+(IFX(m->bmax[1])>>8)*s+FX(1),az0=c.z+(IFX(m->bmin[2])>>8)*s-FX(1),az1=c.z+(IFX(m->bmax[2])>>8)*s+FX(1);
        if(pp.x+PLAYER_MAXS[0]>ax0&&pp.x+PLAYER_MINS[0]<ax1&&pp.y+PLAYER_MAXS[1]>ay0&&pp.y+PLAYER_MINS[1]<ay1&&pp.z+PLAYER_MAXS[2]>az0&&pp.z+PLAYER_MINS[2]<az1)pickup(o);
        break;}
      case K_TRIGGER: {
        bool inside=contains(o,&pp,0); int ph=o->triggerPhase; o->inside=inside;
        if(inside&&o->d->p[6]&&o->visible){const s32 *w=gd.wind+(o->d->p[6]-1)*3; wind[0]+=w[0];wind[1]+=w[1];wind[2]+=w[2];}
        if(ph==0){if(inside)game_trigger(o,false,true);}
        else if(ph==1){o->triggerPhase=2;{int r=o->d->p[2]; if(r>0)game_damage_fx(mulf32(r<<8,dt),true);}}
        else if(ph==2){ if(inside){int r=o->d->p[2]; if(r>0)game_damage_fx(mulf32(r<<8,dt),true);} else {o->triggerPhase=3;game_event(o,EV_COMMANDONLEAVE,NULL,0);} }
        else o->triggerPhase=0;
        if(levelDone)return;
        break;}
      case K_DOOR: {
        bool within=within_radius(o,&pp);
        if(o->closeAt&&gameTime>=o->closeAt&&!within)game_set_door(o,false);
        if(!o->open&&!o->moving&&(within||touched(o,&pp)))game_set_door(o,true);
        break;}
      case K_BUTTON: {
        bool act=within_radius(o,&pp)||touched(o,&pp);
        bool settled=o->buttonPhase==1||o->buttonPhase==5;
        if(act&&settled&&(o->hasMotion||!o->inside))switch_button(o,!o->switchedOn,false);
        o->inside=act; break;}
      case K_ENEMY:
        if(o->d->model<0)break;
        {int b=o->d->p[0]; if(b==E_MAXD||b==E_MAXJ||b==E_WITCH)boss=true; else if(o->alerted&&o->ambush!=AMB_DORMANT)threat=true;}
        if(!enemiesFrozen)enemy_think(o,dt);
        if(((i+frameNo)&63)==0&&!(o->d->p[7]&4))o->cell=find_cell(o->x,o->y+FX(20),o->z);
        break;
      case K_BEAM: {
        vec3 a={o->x,o->y,o->z},e={IFX(o->d->p[0]),IFX(o->d->p[1]),IFX(o->d->p[2])};
        if(o->d->brush>0){const fx *t=modelState[o->d->brush].t; a.x+=t[0];a.y+=t[1];a.z+=t[2];e.x+=t[0];e.y+=t[1];e.z+=t[2];}
        if(o->closeAt){ if(gameTime>=o->closeAt){o->closeAt=0;game_event(o,EV_COMMANDONENTER,NULL,0);} break; }
        if((o->d->p[5]&4)&&o->inside)break;
        if(!o->visible||!segment_hits_player(&a,&e))break;
        o->inside=true;
        if(o->d->p[3]>0)game_damage_fx(mulf32(o->d->p[3]<<8,dt),true);
        if(o->d->p[7]>=0&&gameTime>=o->soundUntil){audio_level_at(o->d->p[7],&a,127);o->soundUntil=gameTime+FX(1);}
        if((o->d->p[5]&3)!=3){ if(o->d->p[6]>0)o->closeAt=gameTime+(o->d->p[6]<<8); else game_event(o,EV_COMMANDONENTER,NULL,0); }
        break;}
      case K_ACTOR: {
        if(!(o->d->p[0]&1)||o->d->model<0||!o->visible)break;
        const ModelDef *m=&gd.models[o->d->model]; int s=o->d->scale;
        int r=(((m->bmax[0]-m->bmin[0])+(m->bmax[2]-m->bmin[2]))*s)>>10; if(r<5)break; if(r>96)r=96; r+=12;
        vec3 c=mounted(o,0); int dx=(pp.x-c.x)>>12,dz=(pp.z-c.z)>>12,dy=(pp.y-c.y)>>12;
        if(abs(dx)<r&&abs(dz)<r&&dy>-58&&dy<((m->height*s)>>8)){int d=sqrt32(dx*dx+dz*dz); if(d<r&&d>0){fx push=IFX(r-d)/d; vec3 a=player.pos,b={a.x+push*dx,a.y,a.z+push*dz}; Trace t; collide_trace(&t,&a,&b,PLAYER_MINS,PLAYER_MAXS,true); player.pos=t.end;}}
        break;}
    }
    if(ps.health<=0||levelDone)return;
  }
  update_music(threat,boss);
  player.env[0]=wind[0];player.env[1]=wind[1];player.env[2]=wind[2];
  // Falling out of the world costs two points and returns RedCat to the checkpoint.
  if(player.pos.y<((level.models[0].min[1]-350)<<12)){game_damage(2);game_teleport(&checkpoint,-1);}
}
void game_summary(s32 *found,s32 *total){
  found[0]=ps.potions;found[1]=0;found[2]=0;found[3]=ps.secrets;
  int nE=0,nC=0;
  for(u32 i=0;i<gd.nobj;i++){const Object *o=&objects[i]; if(o->d->kind==K_ENEMY){nE++; if(o->health<=0)found[2]++;} if(o->d->kind==K_PICKUP&&o->d->p[0]==0){nC++; if(o->collected)found[1]++;}}
  total[0]=gd.meta->nrPotions>=0?gd.meta->nrPotions:potionTotal;total[1]=gd.meta->nrMoney>=0?gd.meta->nrMoney:nC;total[2]=gd.meta->nrEnemies>=0?gd.meta->nrEnemies:nE;total[3]=gd.meta->nrSecrets>=0?gd.meta->nrSecrets:0;
}
static inline bool cell_visible(u16 cell){
  extern const u8 *renderRow; if(cell==0xFFFF||!renderRow)return true; return (renderRow[cell>>3]>>(cell&7))&1;
}
#define MAX_CANDIDATES 40
#define INPUT_POLYS 6000
#define FAR_LOD 320
void game_draw_actors(void){
  int polys=0; int budget=700;
  int cx=cam.pos.x>>12,cy=cam.pos.y>>12,cz=cam.pos.z>>12;
  s32 sy=sinLerp(cam.yaw),cyw=cosLerp(cam.yaw); s32 fxv=-sy,fzv=-cyw;
  // One overhead light in world space; its direction passes through the view matrix.
  MATRIX_RESTORE=1; GFX_LIGHT_COLOR=(0u<<30)|0x7FFF; GFX_LIGHT_VECTOR=((u32)(-160&1023))|((u32)(-440&1023)<<10)|((u32)(-160&1023)<<20);
  if(playerVisible&&!cameraHidesPlayer){ vec3 p=player.pos;
    bool blink=false;
    if(!blink){
      // RedCat stays readable: his light has a floor, and he keeps his full model.
      u16 l=light_at(&p); int lr=l&31,lg=(l>>5)&31,lb=(l>>10)&31; if(lr<22)lr=22; if(lg<22)lg=22; if(lb<22)lb=22;
      polys+=actor_draw(redcat,false,&p,NULL,0,player.yaw+0x4000,0,0,256,playerClip,playerAnim,playerLoop,RGB15(lr,lg,lb));
      // CRcShadow: a soft patch on the floor below him.
      vec3 up={p.x,p.y+FX(10),p.z},dn={p.x,p.y-FX(600),p.z}; Trace t; collide_trace(&t,&up,&dn,ZERO,ZERO,true);
      if(t.fraction<4096&&!t.startSolid&&t.normal[1]>0){vec3 q=t.end; q.y+=FX(2); render_flat(&q,25,SP_SHADOW,0,26,6); polys++;}
    } }
  budget+=polys;
  // The nearest objects in view get the budget.
  static struct {u16 index; s32 dist;} cand[MAX_CANDIDATES]; int nc=0;
  for(u32 i=0;i<gd.nobj;i++){const Object *o=&objects[i]; const ObjDef *d=o->d; int kind=d->kind;
    if(kind!=K_ENEMY&&kind!=K_ACTOR&&kind!=K_PICKUP)continue;
    if(d->model<0||(!o->visible&&kind!=K_ENEMY)||o->collected)continue;
    if(kind==K_ENEMY?!enemy_visible(o):(!o->enabled||o->health<=0))continue;
    int dx=(o->x>>12)-cx,dz=(o->z>>12)-cz,dy=(o->y>>12)-cy;
    const ModelDef *m=&gd.models[d->model]; int rad=(m->radius*d->scale)>>8;
    int lim=(kind==K_ENEMY?900:kind==K_PICKUP?500:700)+rad; if(abs(dx)>lim||abs(dz)>lim||abs(dy)>lim)continue;
    s32 ahead=(dx*fxv+dz*fzv)>>12; if(ahead<-(rad+80))continue;
    s32 side=(dx*cyw-dz*sy)>>12; if(abs(side)>ahead-(ahead>>2)+rad+rad/2+24)continue;  // 75 degrees wide
    s32 dist=dx*dx+dz*dz+dy*dy; if(dist>lim*lim)continue;
    if(kind==K_ENEMY)dist>>=2;   // enemies count as half as far away as decoration
    if(d->scale<=384&&!cell_visible(o->cell))continue;
    if(kind==K_ACTOR&&!attached_visible(o))continue;
    if(nc==MAX_CANDIDATES&&dist>=cand[nc-1].dist)continue;
    int at=nc<MAX_CANDIDATES?nc++:nc-1;
    while(at>0&&cand[at-1].dist>dist){cand[at]=cand[at-1];at--;}
    cand[at].index=i;cand[at].dist=dist;
  }
  for(int n=0;n<nc;n++){const Object *o=&objects[cand[n].index]; const ObjDef *d=o->d; int kind=d->kind;
    const ModelDef *m=&gd.models[d->model]; int rad=(m->radius*d->scale)>>8;
    int nearDist=FAR_LOD+rad; if(kind==K_ENEMY)nearDist>>=1; bool far=cand[n].dist>nearDist*nearDist;
    if(polys+(far?m->polysFar:m->polys)>budget){ if(far||polys+m->polysFar>budget)continue; far=true; }
    vec3 p; int yaw=o->yaw,pitchX=0,rollZ=0,clip=-1; fx t=gameTime,age=0; bool loop=true; const s16 *rot=NULL;
    u16 color;
    if(kind==K_ENEMY){
      p=(vec3){o->x,o->y,o->z};
      clip=o->clip; loop=o->clipLoop; age=0;
      t=mulf32(o->animTime,o->animRate); if(o->animRate<0)t+=clip_seconds(o,clip);
      color=light_at(&p);
    } else {
      p=mounted(o,kind==K_PICKUP?o->pickupY:0);
      if(d->brush>0&&modelState[d->brush].rotated)rot=modelState[d->brush].rot;
      age=gameTime; pitchX=d->p[3]; rollZ=d->p[4];
      if(kind==K_PICKUP){t=0;color=RGB15(31,31,31);}
      else {clip=d->p[6]; t=(s32)(((s64)gameTime*d->p[7])>>8); color=light_at(&p);}
    }
    polys+=actor_draw(d->model,far,&p,rot,age,yaw,pitchX,rollZ,d->scale,clip,t,loop,color);
  }
  polys+=fairy_draw();
  polys+=fx_draw();
  for(int i=0;i<MAX_SHOTS;i++){Shot *s=&shots[i]; if(!s->active)continue;
    render_billboard(&s->pos,s->size,s->sprite,s->color,31,7+i%3,0); polys++;
  }
  // The target marker: rotating, cycling colour, pulled ten units towards the camera.
  if(target&&!cutscene&&target_ok(target)){
    vec3 q; target_point(target,&q); int dx=(cam.pos.x-q.x)>>8,dy=(cam.pos.y-q.y)>>8,dz=(cam.pos.z-q.z)>>8; int len=sqrt32((dx>>2)*(dx>>2)+(dy>>2)*(dy>>2)+(dz>>2)*(dz>>2))<<2; if(len){q.x+=FX(10)/len*dx;q.y+=FX(10)/len*dy;q.z+=FX(10)/len*dz;}
    int phase=gameTime>>2,tri=phase&4095; tri=tri<2048?tri:4095-tri; int width=25+(tri*25>>11);
    u16 col=RGB15(15+((sinLerp(phase*3)+4096)>>9),15+((sinLerp(phase*3+10922)+4096)>>9),15+((sinLerp(phase*3+21845)+4096)>>9));
    render_billboard(&q,width,SP_TARGET,col,31,3,(-gameTime*9)>>1); polys++;
  }
  // Back faces and polygons off screen do not count against the hardware's 2048, so more than that can be sent.
  statPolyBudget=INPUT_POLYS-polys;
}
