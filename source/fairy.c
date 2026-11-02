// Fleurifee, the guiding fairy. Her flight, orbiters and sparks follow NativeFairyEffect in the main port,
// at a lower sprite count: a sprite core with a pulsing halo, two ray quads, five orbiting stars and a small spark pool.
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define SPARKS 10
#define STEP (1.0f/60)
typedef struct {bool active; float p[3],v[3],age,life,wiggle; bool wiggleUp;} Spark;
static struct {
  Object *o; bool active,stop,seeking; float pos[3],vel[3],last[3]; int wp,nwp; const float *wps; float time,start,life,nextSlow,nextBurst,remainder;
  float color[3]; int channel; bool colorUp; float pulse,other; bool pulseUp; float angle; u32 rng; float tips[5][3];
} F;
static Spark sparks[SPARKS];
static float rnd(void){F.rng=F.rng*214013+2531011; return (float)(((F.rng>>16)&32767)%10000);}
void fairy_reset(void){memset(&F,0,sizeof F);memset(sparks,0,sizeof sparks);F.pulse=1;F.other=1;F.pulseUp=true;F.colorUp=true;F.color[0]=20;F.color[1]=255;F.color[2]=20;F.rng=1;}
void fairy_clear(void){F.active=false;F.o=NULL;memset(sparks,0,sizeof sparks);audio_fairy_idle(false);}
static void orbit(int i,float age,float *out){
  float phase=.7f*i+5*age,tilt=1.4f*i,y=30*sinf(phase);
  out[0]=F.pos[0]+30*cosf(phase);out[1]=F.pos[1]+y*cosf(tilt);out[2]=F.pos[2]+y*sinf(tilt);
}
void fairy_enable(Object *o,bool on){
  if(!on){ if(F.o==o&&F.active)F.stop=true; return; }
  F.o=o;F.active=true;F.stop=false;F.seeking=true;
  F.pos[0]=fixedToFloat(o->x,12);F.pos[1]=fixedToFloat(o->y,12);F.pos[2]=fixedToFloat(o->z,12);
  memcpy(F.last,F.pos,12);memset(F.vel,0,12);F.wp=0;F.nwp=o->d->p[1];F.wps=gd.waypoints+o->d->p[2]*3;
  F.life=o->d->p[0]/16.0f;F.start=F.time;F.nextSlow=F.time+.25f;F.nextBurst=F.time+2.5f;F.remainder=0;F.angle=0;F.rng=(o-objects)*7919+1;
  for(int i=0;i<5;i++)orbit(i,0,F.tips[i]);
  audio_sfx(S_FAIRY_APPEAR); audio_fairy_idle(true);
}
static void spawn(float speed,bool burst){
  for(int i=0;i<SPARKS;i++)if(!sparks[i].active){Spark *p=&sparks[i];
    float x=rnd()*.0002f-1,y=rnd()*.0001f,z=rnd()*.0002f-1; float sign=(i%4==0?1:-1)*(burst?-1:1);
    p->active=true;memcpy(p->p,burst?F.last:F.pos,12);p->v[0]=x*speed;p->v[1]=y*sign*speed;p->v[2]=z*speed;p->age=0;p->life=4;p->wiggle=0;p->wiggleUp=true;return;}
}
static bool step_centre(float dt){
  if(!F.nwp)return false;
  int last=F.nwp-1; bool hover=!F.seeking;
  if(!hover&&F.wp>=last){float d=1-.5f*dt; for(int i=0;i<3;i++)F.vel[i]*=d; if(sqrtf(F.vel[0]*F.vel[0]+F.vel[1]*F.vel[1]+F.vel[2]*F.vel[2])<.5f)F.seeking=false;}
  const float *w=F.wps+(hover?last:F.wp)*3; float d[3]={w[0]-F.pos[0],w[1]-F.pos[1],w[2]-F.pos[2]}; float dist=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
  if(hover&&dist<20){d[0]=w[0]+14-F.pos[0];d[1]=w[1]+14-F.pos[1];d[2]=w[2]-F.pos[2];dist=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);}
  else if(!hover&&dist<20&&F.wp<last)F.wp++;
  float mag=.006f*dist*dist; if(mag>200)mag=200;
  float acc[3]; for(int i=0;i<3;i++)acc[i]=dist>0?d[i]/dist*mag:0;
  if(hover){acc[2]+=rnd()*.0005f-2.5f;acc[0]+=rnd()*.0003f-1.5f;}
  for(int i=0;i<3;i++){F.vel[i]+=acc[i]*32*dt;F.pos[i]+=F.vel[i]*dt;}
  float sp=sqrtf(F.vel[0]*F.vel[0]+F.vel[1]*F.vel[1]+F.vel[2]*F.vel[2]);
  if(sp>75)for(int i=0;i<3;i++)F.vel[i]*=75/sp;
  return true;
}
static void tick(float dt){
  F.time+=dt; bool was=F.active; float age=F.time-F.start;
  if(F.active){
    // The automatic lifetime waits for a running conversation; a script Disable stops her at once.
    float life=cutscene&&dialogue_busy()?0:F.life;
    if(F.stop||(life>0&&age>=life))F.active=false; else F.active=step_centre(dt);
  }
  for(int i=0;i<SPARKS;i++)if(sparks[i].active){Spark *p=&sparks[i]; p->age+=dt; if(p->age>=p->life){p->active=false;continue;}
    float q=fabsf(p->wiggle)/.8f,st=(2*(1-q)+(q>.7f?.4f:0))*dt; p->wiggle+=p->wiggleUp?st:-st;
    if(p->wiggle>.8f){p->wiggle=.8f;p->wiggleUp=false;} else if(p->wiggle<-.8f){p->wiggle=-.8f;p->wiggleUp=true;}
    p->v[0]+=p->wiggle*32*dt;p->v[1]+=-1.5f*32*dt;p->v[2]+=p->wiggle*32*dt; for(int k=0;k<3;k++)p->p[k]+=p->v[k]*dt;}
  if(F.active){
    if(F.time>=F.nextSlow){spawn(30,false);F.nextSlow=F.time+.25f;}
    else if(F.time>=F.nextBurst){for(int i=0;i<3;i++)spawn(110,true);F.nextBurst=F.time+2.5f;}
    F.color[F.channel]+=(F.colorUp?1:-1)*500*dt;
    if(F.color[F.channel]>255||F.color[F.channel]<20){F.color[F.channel]=F.colorUp?255:20;F.colorUp=!F.colorUp;F.channel=(F.channel+1)%3;}
    float n=fabsf(2*(F.pulse-1)-1),dl=(.7f*(1-n)+.07f)*dt*(F.pulseUp?1:-1); F.pulse+=dl;F.other-=dl;
    if(F.pulse>2){F.pulse=2;F.other=1;F.pulseUp=false;} else if(F.pulse<1){F.pulse=1;F.other=2;F.pulseUp=true;}
    F.angle+=.9f*dt;
    memcpy(F.last,F.pos,12);
    for(int i=0;i<5;i++)orbit(i,age,F.tips[i]);
  } else if(was){
    for(int i=0;i<SPARKS;i++)spawn(110,true);
    if(F.o)F.o->enabled=false;
    audio_fairy_idle(false); audio_sfx(S_FAIRY_LEAVE);
  }
}
void fairy_update(fx dt){
  F.remainder+=fixedToFloat(dt,12); int guard=0;
  while(F.remainder>=STEP&&guard++<8){tick(STEP);F.remainder-=STEP;}
}
static inline vec3 fv(const float *p){return (vec3){floattof32(p[0]),floattof32(p[1]),floattof32(p[2])};}
int fairy_draw(void){
  int polys=0; int id=10;
  u16 c=RGB15(((int)F.color[0])>>3,((int)F.color[1])>>3,((int)F.color[2])>>3),c2=RGB15(((int)F.color[0])>>3,31,((int)F.color[2])>>3),c3=RGB15(((int)F.color[2])>>3,((int)F.color[1])>>3,((int)F.color[0])>>3);
  int ang=(int)(F.angle*32768/6.28318f);
  if(F.active&&F.o){
    vec3 p=fv(F.pos);
    render_billboard(&p,60,0,0x7FFF,24,id++,0);
    render_billboard(&p,(int)(15*F.pulse),1,c2,22,id++,-ang);
    render_billboard(&p,(int)(15*F.pulse),2,c,12,id++,ang);
    render_billboard(&p,(int)(15*F.other),2,c3,12,id++,ang+4096);
    polys+=4;
    for(int i=0;i<5;i++){vec3 q=fv(F.tips[i]); render_billboard(&q,6,3,c2,31,id++,0); polys++;}
  }
  for(int i=0;i<SPARKS;i++)if(sparks[i].active){Spark *s=&sparks[i]; float o=(s->life-s->age)/3; if(o>1)o=1; int a=(int)(o*31); if(a<1)continue; vec3 q=fv(s->p); render_billboard(&q,6,3,c2,a,id++,0); polys++;}
  return polys;
}
