// Sprite effects: a small particle pool for explosions, smoke and rewards, plus torch flames, coronas, beams,
// save point beacons, teleporters and flash bursts drawn from their objects without any stored particles.
#include "game.h"
#include <string.h>
#define PARTICLES 28
typedef struct {bool active; u8 sprite,s0,s1,a0,a1; s8 spin; u16 color; vec3 p; fx vx,vy,vz,g,age,life;} Particle;
static Particle pool[PARTICLES];
static int nSpout,nCorona,nBeam,nSave,nTeleport,nFlash; static u16 lists[6][128]; static u32 rng=1;
static int cursor;
static inline int rnd(int n){rng=rng*214013+2531011; return ((rng>>16)&32767)%n;}
void fx_reset(void){
  memset(pool,0,sizeof pool); nSpout=nCorona=nBeam=nSave=nTeleport=nFlash=0;
  for(u32 i=0;i<gd.nobj;i++){const ObjDef *d=&gd.objs[i];
    if(d->kind==K_SPOUT&&nSpout<128)lists[0][nSpout++]=i;
    if(d->kind==K_CORONA&&nCorona<128)lists[1][nCorona++]=i;
    if(d->kind==K_BEAM&&nBeam<128)lists[2][nBeam++]=i;
    if(d->kind==K_SAVEPOINT&&nSave<128)lists[3][nSave++]=i;
    if(d->cls==C_TELEPORTER&&nTeleport<128)lists[4][nTeleport++]=i;
    if(d->cls==C_FLASH&&nFlash<128)lists[5][nFlash++]=i;
  }
}
void fx_spawn(int sprite,const vec3 *p,fx vx,fx vy,fx vz,fx life,int s0,int s1,int a0,int a1,u16 color,fx gravity,int spin){
  for(int i=0;i<PARTICLES;i++)if(!pool[i].active){Particle *q=&pool[i]; q->active=true;q->sprite=sprite;q->p=*p;q->vx=vx;q->vy=vy;q->vz=vz;q->life=life;q->age=0;q->s0=s0;q->s1=s1;q->a0=a0;q->a1=a1;q->color=color;q->g=gravity;q->spin=spin;return;}
}
void fx_update(fx dt){
  for(int i=0;i<PARTICLES;i++){Particle *q=&pool[i]; if(!q->active)continue;
    q->age+=dt; if(q->age>=q->life){q->active=false;continue;}
    q->vy-=mulf32(q->g,dt); q->p.x+=mulf32(q->vx,dt);q->p.y+=mulf32(q->vy,dt);q->p.z+=mulf32(q->vz,dt);
  }
}
void fx_explosion(const vec3 *p,int size){
  int s=size<8?8:size>120?120:size;
  fx_spawn(SP_KABOOM,p,0,0,0,FX(.7),s/2,s*2,31,4,RGB15(31,28,16),0,3);
  for(int i=0;i<4;i++)fx_spawn(SP_EXPL,p,IFX(rnd(120)-60),IFX(rnd(80)),IFX(rnd(120)-60),FX(.5)+rnd(1024),s/3,s,31,6,0x7FFF,FX(100),i-2);
}
void fx_smoke(const vec3 *p){
  for(int i=0;i<3;i++)fx_spawn(SP_SMOKE_BROWN,p,IFX(rnd(30)-15),IFX(10+rnd(20)),IFX(rnd(30)-15),FX(1.2),14,34,22,0,0x7FFF,0,0);
}
void fx_impact(const vec3 *p,int kind){
  static const u16 tint[3]={RGB15(31,24,6),RGB15(10,30,28),RGB15(8,28,10)}; static const u8 size[3]={16,12,24};
  fx_spawn(SP_EXPL,p,0,0,0,FX(.4),size[kind],size[kind]*2,31,6,tint[kind],0,2);
  if(kind==2)fx_spawn(SP_RING,p,0,0,0,FX(.5),10,50,24,2,RGB15(8,31,12),0,0);
}
void fx_reward(const vec3 *p,int score,bool pickup){
  int s=score==5?SP_SCORE0:score==15?SP_SCORE1:score==25?SP_SCORE2:score==50?SP_SCORE3:score==75?SP_SCORE4:-1;
  fx life=pickup?FX(1):FX(1.5);
  if(s>=0)fx_spawn(s,p,0,divf32(FX(75),life),0,life,4,40,31,3,0x7FFF,0,0);
  if(pickup)for(int i=0;i<5;i++){int a=i*6553; fx_spawn(SP_KABOOM,&(vec3){p->x+mulf32(cosLerp(a),FX(30)),p->y,p->z+mulf32(sinLerp(a),FX(30))},mulf32(cosLerp(a),-FX(30)),0,mulf32(sinLerp(a),-FX(30)),FX(1),14,4,28,8,RGB15(rnd(32),rnd(32),31),0,4);}
}
void fx_flash(Object *o){o->bossT=gameTime+1;}
void fx_teleporter(Object *o){ if(o->enabled&&(!o->bossT||gameTime-o->bossT>=FX(6.8)))o->bossT=gameTime+1; }
static inline bool near_view(fx x,fx y,fx z,int rangeSq,int *dist){
  int cx=(x-cam.pos.x)>>12,cy=(y-cam.pos.y)>>12,cz=(z-cam.pos.z)>>12;
  if(abs(cx)>600||abs(cy)>600||abs(cz)>600)return false;
  s32 sy=sinLerp(cam.yaw),cyw=cosLerp(cam.yaw); s32 ahead=(cx*-sy+cz*-cyw)>>12; if(ahead<-40)return false;
  s32 side=(cx*cyw-cz*sy)>>12; if(abs(side)>ahead+60)return false;
  int d=cx*cx+cy*cy+cz*cz; if(d>rangeSq)return false; *dist=d; return true;
}
static inline bool pvs(u16 cell){extern const u8 *renderRow; return cell==0xFFFF||!renderRow||((renderRow[cell>>3]>>(cell&7))&1);}
#define MAX_SPOUTS 8
#define MAX_CORONAS 6
int fx_draw(void){
  int polys=0,id=30;
  // Particles.
  for(int i=0;i<PARTICLES;i++){Particle *q=&pool[i]; if(!q->active)continue;
    s32 f=divf32(q->age,q->life); int size=q->s0+(((q->s1-q->s0)*f)>>12),alpha=q->a0+(((q->a1-q->a0)*f)>>12); if(alpha<1)continue;
    render_billboard(&q->p,size,q->sprite,q->color,alpha,id+(i&15),(q->age>>1)*q->spin); polys++;
  }
  id=46;
  // Torch flames and smoke columns: two sprites per emitter, for the nearest ones.
  {static struct {u16 i; s32 d;} near[MAX_SPOUTS]; int n=0;
   for(int k=0;k<nSpout;k++){const Object *o=&objects[lists[0][k]]; if(!o->enabled||!o->visible)continue; int d; if(!near_view(o->x,o->y,o->z,500*500,&d))continue; if(!pvs(o->cell))continue;
     if(n==MAX_SPOUTS&&d>=near[n-1].d)continue; int at=n<MAX_SPOUTS?n++:n-1; while(at>0&&near[at-1].d>d){near[at]=near[at-1];at--;} near[at].i=lists[0][k];near[at].d=d;}
   for(int k=0;k<n;k++){const Object *o=&objects[near[k].i]; const s16 *p=o->d->p; fx life=p[6]<<8; if(life<FX(.2))life=FX(.2);
     for(int j=0;j<2;j++){
       fx age=(gameTime+((near[k].i*1733+j*(life>>1))&0xFFFF))%life; s32 f=divf32(age,life);
       int size=(p[1]*(p[2]+(((p[3]-p[2])*f)>>12)))/100,al=(31*(p[4]+(((p[5]-p[4])*f)>>12)))/100; if(al<1)continue;
       vec3 q={o->x,o->y+mulf32(p[7]<<8,age),o->z}; if(o->d->brush>0){q.x+=modelState[o->d->brush].t[0];q.y+=modelState[o->d->brush].t[1];q.z+=modelState[o->d->brush].t[2];}
       render_billboard(&q,size,p[0],0x7FFF,al,id++,0); polys++; if(id>62)id=46;
     }}
  }
  // Coronas, the glows on lamps and switches. One sight check per frame keeps them from shining through walls.
  if(nCorona){
    Object *c=&objects[lists[1][cursor++%nCorona]]; int dd;
    if(c->enabled&&near_view(c->x,c->y,c->z,600*600,&dd)){c->seen=game_los(&cam.pos,&(vec3){c->x,c->y,c->z});}
    static struct {u16 i; s32 d;} near[MAX_CORONAS]; int n=0;
    for(int k=0;k<nCorona;k++){const Object *o=&objects[lists[1][k]]; if(!o->enabled||!o->visible||!o->seen)continue; int d; if(!near_view(o->x,o->y,o->z,500*500,&d))continue;
      if(n==MAX_CORONAS&&d>=near[n-1].d)continue; int at=n<MAX_CORONAS?n++:n-1; while(at>0&&near[at-1].d>d){near[at]=near[at-1];at--;} near[at].i=lists[1][k];near[at].d=d;}
    for(int k=0;k<n;k++){const Object *o=&objects[near[k].i]; const s16 *p=o->d->p; int dist=sqrt32(near[k].d),span=p[4]-p[3]; if(span<1)span=1;
      int t=dist<p[3]?0:dist>p[4]?span:dist-p[3]; int r=p[1]+((p[2]-p[1])*t)/span;
      vec3 q={o->x,o->y,o->z}; render_billboard(&q,r*2+2,SP_CORONA,(u16)p[0],24,id++,0); polys++; if(id>62)id=46;}
  }
  // Beams, in pieces short enough for the vertex range.
  for(int k=0;k<nBeam;k++){const Object *o=&objects[lists[2][k]]; if(!o->enabled||!o->visible)continue; int d; if(!near_view(o->x,o->y,o->z,1200*1200,&d)&&!near_view(IFX(o->d->p[0]),IFX(o->d->p[1]),IFX(o->d->p[2]),1200*1200,&d))continue;
    if(polys>70)break;
    vec3 a={o->x,o->y,o->z},e={IFX(o->d->p[0]),IFX(o->d->p[1]),IFX(o->d->p[2])}; if(o->d->brush>0){const fx *t=modelState[o->d->brush].t; a.x+=t[0];a.y+=t[1];a.z+=t[2];e.x+=t[0];e.y+=t[1];e.z+=t[2];}
    int len=(abs(e.x-a.x)+abs(e.y-a.y)+abs(e.z-a.z))>>12; int parts=len/400+1; if(parts>6)parts=6;
    for(int j=0;j<parts;j++){vec3 s={a.x+(e.x-a.x)/parts*j,a.y+(e.y-a.y)/parts*j,a.z+(e.z-a.z)/parts*j},t={a.x+(e.x-a.x)/parts*(j+1),a.y+(e.y-a.y)/parts*(j+1),a.z+(e.z-a.z)/parts*(j+1)};
      render_strip(&s,&t,o->d->p[4]*2,SP_BEAM,0x7FFF,24,id++); polys++;}
    if(id>62)id=46;
  }
  // Save point beacons.
  for(int k=0;k<nSave;k++){const Object *o=&objects[lists[3][k]]; int d; if(!near_view(o->x,o->y,o->z,500*500,&d)||!pvs(o->cell))continue;
    vec3 q={o->x,o->y+FX(30),o->z}; render_billboard(&q,36,SP_HALO,RGB15(20,28,31),18,id++,gameTime>>2); polys++; if(id>62)id=46;
  }
  // Teleporter sparks: five stars closing in on the middle.
  for(int k=0;k<nTeleport;k++){Object *o=&objects[lists[4][k]]; if(!o->bossT)continue; fx age=gameTime-o->bossT; if(age>=FX(6.8)){o->bossT=0;continue;} int d; if(!near_view(o->x,o->y,o->z,600*600,&d))continue;
    s32 f=age>=FX(2.1)?4096:divf32(age,FX(2.1)); int r=100+((10-100)*f>>12);
    for(int j=0;j<5;j++){int a=j*6553-(int)(((s64)age*39114)>>12); vec3 q={o->x+r*cosLerp(a),o->y+mulf32(age,FX(15)),o->z+r*sinLerp(a)}; render_billboard(&q,14,SP_STAR,RGB15(20,28,31),26,id++,0); polys++; if(id>62)id=46;}
  }
  // Flash bursts grow and shrink over one second, then disable themselves.
  for(int k=0;k<nFlash;k++){Object *o=&objects[lists[5][k]]; if(!o->bossT)continue; fx age=gameTime-o->bossT; if(age>=FX(1)){o->bossT=0;o->enabled=false;continue;}
    int env=age<FX(.5)?age:FX(1)-age; vec3 q={o->x,o->y,o->z};
    render_billboard(&q,8+((env*100)>>12),SP_KABOOM,RGB15(29,29,31),26,id++,(mulf32(age,age)*31290)>>12); render_billboard(&q,6+((env*65)>>12),SP_RAYS,RGB15(10,8,31),20,id++,age>>1); polys+=2; if(id>60)id=46;
  }
  return polys;
}
