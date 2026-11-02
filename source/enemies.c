// Enemy behaviour, after thinkEnemy and its helpers in the main port: sight, memory, patrols over the level's
// waypoints, ambushes, salvos, touch pursuit and the three boss machines.
#include "game.h"
#include <stdlib.h>
#include <string.h>
enum {B_IDLE,B_SHOOT,B_BETWEEN,B_RISE,B_LOOK,B_LOWER,B_TP_OUT,B_INVISIBLE,B_TP_IN,B_START,B_FLY,B_RECOVER,B_SALVOWAIT};
const EnemyStat *enemy_stat(const Object *o){return &gd.stats[o->d->p[1]*3+gameDifficulty];}
static inline int kind_of(const Object *o){return o->d->p[0];}
static int roll(Object *o){u32 s=o->rng; if(!s)s=(u32)(o-objects)*2654435761u+1; s^=s<<13;s^=s>>17;s^=s<<5; o->rng=s; return (s>>9)&0x7fff;}
static inline fx sec16(int v){return v<<8;}
static void hull(const Object *o,fx *mn,fx *mx){
  const ModelDef *m=&gd.models[o->d->model]; int s=o->d->scale;
  int w=(((m->bmax[0]-m->bmin[0])+(m->bmax[2]-m->bmin[2]))*s)>>10; if(w<8)w=8; if(w>20)w=20;
  int h=(m->bmax[1]*s)>>8; if(h<16)h=16; if(h>64)h=64;
  mn[0]=-IFX(w);mn[1]=0;mn[2]=-IFX(w);mx[0]=IFX(w);mx[1]=IFX(h);mx[2]=IFX(w);
}
void enemy_aim(const Object *o,vec3 *out){fx mn[3],mx[3]; hull(o,mn,mx); *out=(vec3){o->x,o->y+mx[1]-(mx[1]>>2)+(mx[1]>>4),o->z};}
fx clip_seconds(const Object *o,int clip){
  if(clip<0||clip>=gd.models[o->d->model].nclips)return FX(1);
  const ClipDef *c=&gd.clips[gd.models[o->d->model].clip+clip]; return (c->frames<<12)/c->rate;
}
// Picks a clip and returns how long it plays at this rate.
static fx anim(Object *o,int ai,const char *name,const char *alt,fx rate,bool loop){
  int c=model_clip(o->d->model,name); if(c<0&&alt)c=model_clip(o->d->model,alt);
  o->ai=ai;o->clip=c<0?255:c;o->animTime=0;o->animRate=rate;o->clipLoop=loop;
  fx dur=divf32(clip_seconds(o,c),abs(rate)); o->animUntil=gameTime+dur; return dur;
}
static fx anim_idle(Object *o){ if(o->ai==A_IDLE)return 0; return anim(o,A_IDLE,"idle","idle0",4096,true); }
static fx anim_walk(Object *o){ if(o->ai==A_WALK)return 0; return anim(o,A_WALK,"walkfw","walk",4096,true); }
static fx anim_attack(Object *o,fx rate){return anim(o,A_ATTACK,"shoot1","shoot",rate,false);}
static void say(Object *o,int slot){
  if(gameTime<o->soundUntil)return; int id=enemy_stat(o)->snd[slot]; if(id<0)return;
  vec3 p={o->x,o->y,o->z}; int ms=audio_level_at(id,&p,127); if(ms>0)o->soundUntil=gameTime+(ms*4096)/1000;
}
static inline int dist3(int x,int y,int z){return sqrt32(x*x+y*y+z*z);}
static void face(Object *o,fx x,fx z,fx dt){
  int want=atan2_bin((x-o->x)>>4,(z-o->z)>>4),diff=((want-o->yaw+16384)&0x7fff)-16384,turn=(mulf32(enemy_stat(o)->rot4,dt))*4;
  if(diff>turn)diff=turn; if(diff<-turn)diff=-turn; o->yaw=o->yaw+diff;
}
// The main port's moveEnemy: slide on walls, step over low edges, fall.
static void move(Object *o,fx dx,fx dy,fx dz,fx dt){
  const bool flying=o->d->p[7]&2; fx mn[3],mx[3]; hull(o,mn,mx);
  vec3 start={o->x,o->y,o->z},end={o->x+dx,o->y+dy,o->z+dz},p=start; Trace t;
  if(dx||dy||dz){
    collide_trace(&t,&start,&end,mn,mx,true); p=t.end;
    if(t.fraction<4096&&!t.startSolid&&(flying||(o->grounded&&abs(t.normal[1])<(s32)(.65*(1<<30))))){
      s32 rem=4096-t.fraction; fx rx=mulf32(dx,rem),ry=mulf32(dy,rem),rz=mulf32(dz,rem);
      s32 in=(s32)(((s64)rx*t.normal[0]+(s64)ry*t.normal[1]+(s64)rz*t.normal[2])>>30);
      if(in<0){vec3 e={p.x+rx-(s32)(((s64)t.normal[0]*in)>>30),p.y+ry-(s32)(((s64)t.normal[1]*in)>>30),p.z+rz-(s32)(((s64)t.normal[2]*in)>>30)}; Trace s; collide_trace(&s,&p,&e,mn,mx,true); p=s.end;}
    }
    if(!flying&&o->grounded&&t.fraction<4096){
      vec3 up={start.x,start.y+FX(16),start.z}; Trace u; collide_trace(&u,&start,&up,mn,mx,true);
      if(u.fraction==4096){vec3 e={up.x+dx,up.y,up.z+dz}; Trace f; collide_trace(&f,&up,&e,mn,mx,true); vec3 dn={f.end.x,f.end.y-FX(18),f.end.z}; Trace g; collide_trace(&g,&f.end,&dn,mn,mx,true);
        if(g.fraction<4096&&g.normal[1]>(s32)(.65*(1<<30))&&abs(g.end.x-start.x)+abs(g.end.z-start.z)>abs(p.x-start.x)+abs(p.z-start.z))p=g.end;}
    }
  }
  if(!flying){
    const fx fall=IFX(enemy_stat(o)->fallSpeed);
    o->vy-=mulf32(FX(800),dt); if(o->vy<-fall)o->vy=-fall;
    vec3 e={p.x,p.y+mulf32(o->vy,dt),p.z}; Trace f; collide_trace(&f,&p,&e,mn,mx,true); p=f.end;
    o->grounded=f.fraction<4096&&f.normal[1]>(s32)(.65*(1<<30))&&o->vy<=0; if(f.fraction<4096)o->vy=0;
  }
  o->x=p.x;o->y=p.y;o->z=p.z;
}
static bool walk_to(Object *o,fx x,fx y,fx z,fx dt){
  face(o,x,z,dt);
  const bool flying=o->d->p[7]&2; int dx=(x-o->x)>>8,dy=flying?(y-o->y)>>8:0,dz=(z-o->z)>>8; int len=dist3(dx>>2,dy>>2,dz>>2)<<2; if(!len)len=1;
  fx step=mulf32(IFX(enemy_stat(o)->speed),dt); fx rem=len<<8; if(step>rem)step=rem;
  fx bx=o->x,bz=o->z,by=o->y;
  s32 f=divf32(step,len<<8); move(o,mulf32(dx<<8,f),mulf32(dy<<8,f),mulf32(dz<<8,f),dt);
  bool moved=abs(o->x-bx)+abs(o->z-bz)+abs(o->y-by)>FX(.02);
  if(moved)anim_walk(o); else anim_idle(o);
  o->stuck=moved?0:o->stuck+dt;
  return moved;
}
static void stand(Object *o,fx dt){ if(o->d->p[7]&4)return; move(o,0,0,0,dt); }
// One waypoint at a time; when RedCat is known, the link that leads closest to him.
static int wp_nearest(fx x,fx y,fx z){
  int best=-1; s32 bd=0x7fffffff; const int n=gd.meta->nWaypoints; int px=x>>12,py=y>>12,pz=z>>12;
  for(int i=0;i<n;i++){const WayPoint *w=&gd.wps[i]; int dx=w->x-px,dy=w->y-py,dz=w->z-pz; if(abs(dx)>800||abs(dy)>400||abs(dz)>800)continue; s32 d=dx*dx+dy*dy*2+dz*dz; if(d<bd){bd=d;best=i;}}
  return best;
}
static void wp_choose(Object *o,bool closer){
  if(o->wpCur<0){o->wpTarget=-1;return;}
  const WayPoint *w=&gd.wps[o->wpCur]; int best=-1,count=0; s32 bd=0x7fffffff; int px=player.pos.x>>12,pz=player.pos.z>>12;
  for(int k=0;k<4;k++){int l=w->links[k]; if(l<0)continue; if(l==o->wpPrev&&(w->links[1]>=0))continue; count++;
    if(closer){const WayPoint *t=&gd.wps[l]; int dx=t->x-px,dz=t->z-pz; s32 d=dx*dx+dz*dz; if(d<bd){bd=d;best=l;}}
    else if(!(roll(o)%count))best=l;}
  if(best<0){for(int k=0;k<4;k++)if(w->links[k]>=0)best=w->links[k];}
  o->wpTarget=best;
}
static bool patrol(Object *o,fx dt,bool closer){
  if(o->wpCur<0&&o->wpTarget<0)return false;
  if(o->wpTarget<0)wp_choose(o,closer);
  if(o->wpTarget<0)return false;
  const WayPoint *w=&gd.wps[o->wpTarget]; const bool flying=o->d->p[7]&2; int dx=w->x-(o->x>>12),dy=flying?w->y-(o->y>>12):0,dz=w->z-(o->z>>12);
  if(dx*dx+dy*dy+dz*dz<16*16){o->wpPrev=o->wpCur;o->wpCur=o->wpTarget;o->wpTarget=-1;o->leftStart=true;wp_choose(o,closer); if(o->wpTarget<0)return false; w=&gd.wps[o->wpTarget];}
  walk_to(o,IFX(w->x),IFX(w->y),IFX(w->z),dt);
  if(o->stuck>FX(1)){o->stuck=0;o->wpPrev=o->wpTarget;int n=wp_nearest(o->x,o->y,o->z); o->wpCur=n; o->wpTarget=-1;}
  return true;
}
static bool sees(Object *o,int d3){
  const EnemyStat *s=enemy_stat(o); const bool spider=kind_of(o)==E_SPIDER; int dx=(player.pos.x-o->x)>>12,dz=(player.pos.z-o->z)>>12;
  int d=spider?dist3(dx,0,dz):d3;
  if(d>s->sense){
    if(d>s->visual)return false;
    int heading=atan2_bin(dx<<4,dz<<4),diff=abs(((heading-o->yaw+16384)&0x7fff)-16384);
    if(diff*2>=s->viewAngle)return false;
  }
  fx mn[3],mx[3]; hull(o,mn,mx);
  vec3 a={o->x,o->y+mx[1]-(mx[1]>>2),o->z},b={player.pos.x,player.pos.y+FX(29),player.pos.z};
  if(game_los(&a,&b))return true; b.y+=FX(57.92); return game_los(&a,&b);
}
static void shoot_now(Object *o){
  const EnemyStat *s=enemy_stat(o); vec3 a; enemy_aim(o,&a); a.y-=FX(6);
  int dx=(player.pos.x-a.x)>>6,dy=(player.pos.y+FX(28)-a.y)>>6,dz=(player.pos.z-a.z)>>6; int len=sqrt32((dx>>4)*(dx>>4)+(dy>>4)*(dy>>4)+(dz>>4)*(dz>>4))<<4; if(!len)len=1;
  s32 dir[3]={divf32(dx,len),divf32(dy,len),divf32(dz,len)};
  fx life=sec16(s->pLifeMin16)+((sec16(s->pLifeMax16-s->pLifeMin16)>>4)*(roll(o)&15));
  game_shot(s->pSprite,&a,dir,IFX(s->pSpeed),IFX(s->pGravity),life,sec16(s->pDamage16),kind_of(o)==E_WITCH);
  say(o,SND_ATTACK);
}
static int salvo_size(Object *o){const EnemyStat *s=enemy_stat(o); int v=1+(((s->avgShots16-16)*(roll(o)%10000))/160000); return v<1?1:v;}
// ---------- bosses ----------
static void boss_enter(Object *o,int phase,fx dur){o->bossPhase=phase;o->bossT=0;o->bossDur=dur;o->fired=false;o->pending=false;}
static void boss_shoot(Object *o){
  fx dur=kind_of(o)==E_MAXD?FX(1):0; fx d=anim_attack(o,4096); boss_enter(o,B_SHOOT,dur?dur:d);
}
static void boss_salvo(Object *o){o->salvo=salvo_size(o);boss_shoot(o);}
static void boss_teleport(Object *o,bool guarded){
  o->bossInvuln=guarded;o->bossHidden=false;
  fx d=anim(o,A_TELEPORT,"teleport","idle",4096,false); boss_enter(o,B_TP_OUT,d);
  vec3 p={o->x,o->y+FX(30),o->z}; fx_explosion(&p,20);
}
static void boss_choose_point(Object *o){
  int count=0,chosen=-1; for(u32 i=0;i<gd.nobj;i++)if(gd.objs[i].cls==C_JESTERPT)count++;
  const int n=count+1; int c=o->bossPoint<0?count:o->bossPoint; chosen=c;
  for(int a=0;a<5&&chosen==c;a++){
    int cand=roll(o)%n; fx x=o->homeX,y=o->homeY,z=o->homeZ; int k=0;
    if(cand<count)for(u32 i=0;i<gd.nobj;i++)if(gd.objs[i].cls==C_JESTERPT){ if(k++==cand){x=gd.objs[i].x;y=gd.objs[i].y;z=gd.objs[i].z;break;} }
    if(dist3((x-player.pos.x)>>12,(y-player.pos.y)>>12,(z-player.pos.z)>>12)>=enemy_stat(o)->minDist){chosen=cand;o->x=x;o->y=y;o->z=z;}
  }
  o->bossPoint=chosen;
}
static void boss_update(Object *o,fx dt,int d3){
  const EnemyStat *s=enemy_stat(o); const int base=kind_of(o); o->bossT+=dt;
  if(!o->bossStarted){o->bossStarted=true;
    if(base==E_WITCH){fx du=anim(o,A_START,"start","idle",4096,false); boss_enter(o,B_START,du);}
    else if(base==E_MAXJ)boss_teleport(o,false);
  }
  const bool knows=o->seen||o->alerted;
  if(base==E_MAXD){
    int k=s->machine16,mn=(s->minH*k)>>4,mx=(s->maxH*k)>>4; if(mx<mn)mx=mn; fx rise=sec16(s->rise16); if(rise<256)rise=256;
    switch(o->bossPhase){
      case B_IDLE: if(knows)boss_salvo(o); break;
      case B_SHOOT:
        if(!o->fired&&o->bossT>=mulf32(o->bossDur,s->drawPart<<4)){o->fired=true;shoot_now(o);}
        if(o->bossT>=o->bossDur){ if(--o->salvo>0)boss_shoot(o); else {anim_idle(o);boss_enter(o,B_RISE,rise);} }
        break;
      case B_RISE: o->y=o->homeY+IFX(mn)+mulf32(IFX(mx-mn),divf32(o->bossT>o->bossDur?o->bossDur:o->bossT,o->bossDur));
        if(o->bossT>=o->bossDur)boss_enter(o,B_LOOK,sec16(s->look16)); break;
      case B_LOOK: if(o->bossT>=o->bossDur)boss_enter(o,B_LOWER,rise); break;
      case B_LOWER: o->y=o->homeY+IFX(mx)-mulf32(IFX(mx-mn),divf32(o->bossT>o->bossDur?o->bossDur:o->bossT,o->bossDur));
        if(o->bossT>=o->bossDur)boss_salvo(o); break;
      case B_RECOVER: if(o->bossT>=o->bossDur)boss_salvo(o); break;
    }
  } else if(base==E_MAXJ){
    switch(o->bossPhase){
      case B_IDLE: boss_teleport(o,false); break;
      case B_TP_OUT: if(o->bossT>=o->bossDur){o->bossHidden=true;boss_enter(o,B_INVISIBLE,FX(.6));} break;
      case B_INVISIBLE: if(o->bossT>=o->bossDur){boss_choose_point(o);o->bossHidden=false;fx du=anim(o,A_TELEPORT,"teleport","idle",4096,false);boss_enter(o,B_TP_IN,du<FX(1)?FX(1):du);vec3 p={o->x,o->y+FX(30),o->z};fx_explosion(&p,20);} break;
      case B_TP_IN: if(o->bossT>=o->bossDur){o->bossInvuln=false; if((roll(o)%10000)*o->maxHealth<o->health*10000)boss_salvo(o); else boss_teleport(o,false);} break;
      case B_SHOOT:
        face(o,player.pos.x,player.pos.z,dt);
        if(!o->fired&&o->bossT>=mulf32(o->bossDur,s->drawPart<<4)){o->fired=true;shoot_now(o);}
        if(o->bossT>=o->bossDur){ if(--o->salvo>0)boss_shoot(o); else boss_teleport(o,true); }
        break;
      case B_RECOVER: if(o->bossT>=o->bossDur)boss_teleport(o,false); break;
    }
  } else {
    switch(o->bossPhase){
      case B_IDLE: boss_enter(o,B_FLY,0); break;
      case B_START: if(o->bossT>=o->bossDur)boss_enter(o,B_FLY,0); break;
      case B_FLY:
        if(knows&&o->leftStart&&d3<s->attackRange){boss_salvo(o);break;}
        if(!patrol(o,dt,knows)){o->leftStart=true;anim_idle(o);}
        break;
      case B_SHOOT:
        face(o,player.pos.x,player.pos.z,dt);
        if(!o->fired&&o->bossT>=mulf32(o->bossDur,s->drawPart<<4)){o->fired=true;shoot_now(o);}
        if(o->bossT>=o->bossDur){
          if(--o->salvo>0){anim_idle(o);boss_enter(o,B_BETWEEN,sec16(s->waitShots16));}
          else if((roll(o)&255)<=s->moveChance)boss_enter(o,B_FLY,0);
          else {anim_idle(o);boss_enter(o,B_SALVOWAIT,sec16(s->waitSalvo16));}
        }
        break;
      case B_BETWEEN: if(o->bossT>=o->bossDur)boss_shoot(o); break;
      case B_SALVOWAIT: if(o->bossT>=o->bossDur){ if(knows&&d3<=s->attackRange)boss_salvo(o); else boss_enter(o,B_FLY,0); } break;
      case B_RECOVER: if(o->bossT>=o->bossDur)boss_enter(o,B_FLY,0); break;
      default: boss_enter(o,B_FLY,0);
    }
  }
  if(base!=E_WITCH&&!o->bossHidden&&o->seen)face(o,player.pos.x,player.pos.z,dt);
}
// ---------- setup ----------
void enemy_init(Object *o){
  const EnemyStat *s=enemy_stat(o); const ObjDef *d=o->d; const int base=d->p[0];
  o->health=o->maxHealth=s->health;o->animRate=4096;o->clip=255;o->clipLoop=true;o->wpCur=d->p[3];o->wpPrev=-1;o->wpTarget=-1;o->bossPoint=-1;
  o->homeX=d->x;o->homeY=d->y;o->homeZ=d->z;o->grounded=false;o->seen=false;o->sightAt=-1;
  anim(o,A_IDLE,"idle","idle0",4096,true);
  o->ambush=AMB_NONE;
  if(base==E_SKELETON||base==E_KNIGHT||base==E_GARGOYLE){o->ambush=AMB_DORMANT;o->ai=A_DORMANT;
    int c=model_clip(d->model,base==E_GARGOYLE?"static":"start"); if(c<0)c=model_clip(d->model,"start"); o->clip=c<0?255:c;o->animRate=0;o->clipLoop=false;}
  if(base==E_MAXD)o->y=o->homeY+IFX((s->minH*s->machine16)>>4);
}
// A spider hangs from the ceiling above its spot, if there is one.
void enemy_place(Object *o){
  if(o->d->p[0]!=E_SPIDER)return;
  fx mn[3],mx[3]; hull(o,mn,mx); static const fx Z[3]={0,0,0};
  vec3 p={o->x,o->y+FX(64),o->z},q={o->x,o->y-FX(256),o->z}; Trace f; collide_trace(&f,&p,&q,Z,Z,true);
  if(f.fraction<4096&&!f.startSolid&&f.normal[1]>(s32)(.65*(1<<30)))o->y=f.end.y+FX(.05);
  vec3 a={o->x,o->y,o->z},b={o->x,o->y+IFX(2048),o->z}; Trace c; collide_trace(&c,&a,&b,mn,mx,true);
  if(c.fraction>=4096||c.startSolid||c.end.y-o->y<FX(16)){o->ambush=AMB_AWAKE;return;}
  o->lowerY=o->y;o->upperY=c.end.y;o->y=c.end.y;o->ambush=AMB_DORMANT;
}
// ---------- per frame ----------
void enemy_think(Object *o,fx dt){
  const EnemyStat *s=enemy_stat(o); const int base=kind_of(o); const bool flying=o->d->p[7]&2,standing=o->d->p[7]&4,ranged=s->ranged;
  o->animTime+=dt;
  if(o->attackTimer>0){o->attackTimer-=dt; if(o->attackTimer<0)o->attackTimer=0;}
  int dx=(player.pos.x-o->x)>>12,dy=(player.pos.y-o->y)>>12,dz=(player.pos.z-o->z)>>12;
  if(abs(dx)>1600||abs(dz)>1600||abs(dy)>1600){o->seen=false;return;}
  // Enemies far from RedCat think four times less often.
  if(abs(dx)+abs(dz)>600){o->thinkDt+=dt; if(((o-objects)+(gameTime>>8))&3)return; dt=o->thinkDt>FX(.25)?FX(.25):o->thinkDt; o->thinkDt=0;}
  int d3=dist3(dx,dy,dz);
  if(gameTime>=o->sightAt){o->sightAt=gameTime+FX(.5);o->seen=sees(o,d3);}
  if(o->seen){ if(!o->alerted){o->alerted=true;say(o,SND_ALERT);} o->memory=gameTime; }
  else if(o->alerted&&gameTime-o->memory>=sec16(s->remember16)){o->alerted=false;say(o,SND_IDLE1);}
  const bool knows=o->seen||o->alerted;
  if(!o->alerted&&(base==E_FROG||base==E_ZOMBIE||base==E_GUARDIAN||base==E_GHOST||base==E_BAT))say(o,(roll(o)&1)&&s->snd[SND_IDLE2]>=0?SND_IDLE2:SND_IDLE1);
  // A zombie climbs out of its grave along the first edge.
  if(o->d->p[4]>=0&&!o->leftStart){
    const WayPoint *w=&gd.wps[o->d->p[4]]; if(o->ai==A_HIT&&gameTime<o->animUntil)return;
    int ex=(w->x<<12)-o->x,ey=(w->y<<12)-o->y,ez=(w->z<<12)-o->z; int len=dist3(ex>>8,ey>>8,ez>>8); fx step=mulf32(IFX(s->speed),dt);
    if(len<<8<=step){o->x=IFX(w->x);o->y=IFX(w->y);o->z=IFX(w->z);o->leftStart=true;o->grounded=true;o->wpPrev=o->wpCur;o->wpCur=o->d->p[4];}
    else {s32 f=divf32(step,len<<8);o->x+=mulf32(ex,f);o->y+=mulf32(ey,f);o->z+=mulf32(ez,f);face(o,IFX(w->x),IFX(w->z),dt);anim_walk(o);}
    return;
  }
  // Ambushes.
  if(base==E_SPIDER){
    if(o->ambush==AMB_DORMANT){
      if(!o->alerted)return;
      o->ambush=AMB_DESCEND;anim_idle(o);
    }
    if(o->ambush==AMB_DESCEND){
      o->y-=mulf32(IFX(s->fallSpeed),dt);
      if(o->y<=o->lowerY){o->y=o->lowerY;o->ambush=AMB_AWAKE;o->grounded=true;o->vy=0;}
      return;
    }
  }
  if(base==E_SKELETON){
    if(o->ai==A_HIT&&gameTime<o->animUntil)return;
    if(o->ambush==AMB_DORMANT){ if(!o->seen)return; o->alerted=true;say(o,SND_ALERT);o->ambush=AMB_WAKING;anim(o,A_START,"start","idle",4096,false);return; }
    if(o->ambush==AMB_WAKING){ if(gameTime>=o->animUntil){o->ambush=AMB_AWAKE;anim_idle(o);} return; }
    if(o->ambush==AMB_RETRACT){
      if(o->alerted||o->seen){o->ambush=AMB_WAKING;anim(o,A_START,"start","idle",4096,false);return;}
      if(gameTime>=o->animUntil){o->ambush=AMB_DORMANT;o->ai=A_DORMANT;o->animRate=0;o->animTime=0;}
      return;
    }
    if(!o->alerted&&!o->pending&&gameTime>=o->animUntil&&gameTime-o->memory>sec16(s->remember16)&&o->memory>0){o->ambush=AMB_RETRACT;anim(o,A_RETRACT,"start","idle",-4096,false);return;}
  }
  if(base==E_KNIGHT||base==E_GARGOYLE){
    if((o->ai==A_HIT)&&gameTime<o->animUntil)return;
    if(o->ambush==AMB_DORMANT){
      if(!o->alerted)return;
      if(base==E_KNIGHT){o->ambush=AMB_WAKING;anim(o,A_START,"start","idle",FX(1.7),false);return;}
      o->ambush=AMB_AWAKE;anim_idle(o);
    }
    if(o->ambush==AMB_WAKING){ if(gameTime<o->animUntil)return; o->ambush=AMB_AWAKE;anim_idle(o); }
    if(o->ambush==AMB_RETRACT){
      if(o->alerted){o->ambush=AMB_WAKING;anim(o,A_START,"start","idle",FX(1.7),false);return;}
      if(gameTime>=o->animUntil){o->ambush=AMB_DORMANT;o->ai=A_DORMANT;o->animRate=0;o->animTime=0;}
      return;
    }
    if(!o->alerted&&!o->pending&&(o->ai!=A_ATTACK||gameTime>=o->animUntil)){
      o->salvo=0;o->attackTimer=0;
      if(base==E_KNIGHT){o->ambush=AMB_RETRACT;anim(o,A_RETRACT,"start","idle",FX(-1.7),false);}
      else {o->ambush=AMB_DORMANT;o->ai=A_DORMANT;o->animRate=0;}
      return;
    }
  }
  if(base==E_MAXD||base==E_MAXJ||base==E_WITCH){ boss_update(o,dt,d3); return; }
  // Touch enemies chase until their body hits RedCat, at most once a second.
  const bool touch=base==E_GUARDIAN||base==E_ZOMBIE||((base==E_SPIDER||base==E_BAT||base==E_GHOST)&&!ranged);
  if(touch){
    bool held=(o->ai==A_HIT||o->ai==A_START)&&gameTime<o->animUntil;
    if(held)stand(o,dt);
    else if(knows){
      vec3 eye={o->x,o->y+FX(20),o->z},to={player.pos.x,player.pos.y+FX(29),player.pos.z};
      bool direct=o->seen||game_los(&eye,&to);
      if(direct){walk_to(o,player.pos.x,flying?player.pos.y+FX(30):o->y,player.pos.z,dt); if(o->stuck>FX(.6)&&gd.meta->nWaypoints){o->stuck=0;o->wpCur=wp_nearest(o->x,o->y,o->z);o->wpTarget=-1;}}
      else if(!patrol(o,dt,true))walk_to(o,player.pos.x,player.pos.y+(flying?FX(30):0),player.pos.z,dt);
    } else if(!patrol(o,dt,false)){stand(o,dt);anim_idle(o);}
    fx mn[3],mx[3]; hull(o,mn,mx); fx rx=mx[0]+FX(12.48);
    if(o->attackTimer==0&&abs(o->x-player.pos.x)<rx&&abs(o->z-player.pos.z)<rx&&player.pos.y<o->y+mx[1]&&player.pos.y+FX(57.92)>o->y){
      vec3 eye={o->x,o->y+mx[1]-(mx[1]>>2),o->z},to={player.pos.x,player.pos.y+FX(29),player.pos.z};
      if(game_los(&eye,&to)){game_damage_fx(sec16(s->damage*16),false);o->attackTimer=FX(1);}
    }
    return;
  }
  // Strikes and shots happen at the authored pose, not when the attack is requested.
  const bool locked=(o->ai==A_ATTACK||o->ai==A_HIT||o->ai==A_START)&&gameTime<o->animUntil;
  if(o->pending&&gameTime>=o->pendAt){
    const bool inWindow=gameTime-dt<=o->pendUntil;
    if(inWindow&&ranged){shoot_now(o);o->pending=false;}
    else if(inWindow&&o->seen&&d3<=s->attackRange+5){game_damage_fx(sec16(s->damage*16),false);o->pending=false;}
    else if(gameTime>=o->pendUntil)o->pending=false;
  }
  if(locked){ if(o->seen||(standing&&knows))face(o,player.pos.x,player.pos.z,dt); stand(o,dt); return; }
  const fx range=s->attackRange,minimum=s->minDist;
  const bool committed=o->salvo>0&&ranged;
  if(o->seen||committed||(base==E_KNIGHT&&knows)){
    if(o->seen||(standing&&knows))face(o,player.pos.x,player.pos.z,dt);
    if((committed||(d3<=range&&d3>=minimum)||(base==E_KNIGHT&&d3<=range))&&o->attackTimer==0){
      fx dur=anim_attack(o,base==E_KNIGHT?FX(1.8):4096);
      fx strike=mulf32(dur,(ranged?s->drawPart:s->strikeStart)<<4);
      o->pending=true;o->pendAt=gameTime+strike;o->pendUntil=base==E_KNIGHT?gameTime+mulf32(dur,s->strikeEnd<<4):gameTime+strike;
      fx wait=(base==E_PLANT||base==E_GARGOYLE||base==E_KNIGHT)?0:sec16(s->waitShots16);
      if(ranged){
        if(o->salvo==0)o->salvo=salvo_size(o);
        if(--o->salvo==0){wait=sec16(s->waitSalvo16);o->relocate=(base!=E_GARGOYLE)&&(roll(o)&255)<s->moveChance;}
      }
      o->attackTimer=dur+wait; stand(o,dt); return;
    }
    if(!standing&&d3<minimum&&d3>0){walk_to(o,o->x-IFX(dx)/d3*(minimum-d3+10),o->y,o->z-IFX(dz)/d3*(minimum-d3+10),dt);return;}
    if(!standing&&d3>(range-(range>>2)>minimum?range-(range>>2):minimum)){walk_to(o,player.pos.x,player.pos.y+(flying?FX(30):0),player.pos.z,dt);return;}
    if(o->relocate&&!standing){o->relocate=false;wp_choose(o,false);}
  } else if(!standing){
    if(o->alerted&&gameTime-o->memory<FX(3)&&abs(dx)+abs(dz)>10){
      if(!patrol(o,dt,true))walk_to(o,player.pos.x,o->y,player.pos.z,dt);
      return;
    }
    if(base!=E_BRUTUSM&&base!=E_BRUTUSB&&patrol(o,dt,false))return;
  }
  stand(o,dt); anim_idle(o);
}
// ---------- being hit ----------
void enemy_hit(Object *o,fx amount){
  if(o->health<=0)return;
  const int base=kind_of(o); o->alerted=true;o->memory=gameTime;
  if(base==E_MAXD||base==E_MAXJ||base==E_WITCH){
    if(o->bossHidden||o->bossInvuln)return;
    int dmg=amount>>12; if(dmg<1)dmg=1;
    bool defeated=o->health-dmg<=(base==E_WITCH?2:0);
    if(defeated){say(o,SND_HURT);enemy_defeat(o);return;}
    o->health-=dmg;
    if((roll(o)%(base==E_MAXJ?3:2))!=0){say(o,SND_HURT);return;}
    if(o->bossPhase==B_RECOVER)return;
    o->salvo=0; fx d=anim(o,A_HIT,"hit","idle",4096,false); boss_enter(o,B_RECOVER,d); say(o,SND_HURT);
    return;
  }
  int r=roll(o); bool react=(r&1)==0; int dmg=amount>>12; if(dmg<1)dmg=1;
  if(base==E_BRUTUSB){react=r%3==0; if(!react)dmg=0;}
  if((base==E_KNIGHT||base==E_GARGOYLE)&&o->ambush==AMB_DORMANT)dmg=0;
  if(dmg>0&&o->health<=dmg){say(o,SND_HURT);enemy_defeat(o);return;}
  o->health-=dmg;
  if(!react){say(o,SND_HURT);return;}
  if(o->ai==A_HIT)return;
  o->pending=false;o->salvo=0;o->relocate=false;
  if(base==E_KNIGHT||base==E_GARGOYLE){o->ambush=AMB_AWAKE;}
  if(base==E_SKELETON)o->ambush=AMB_AWAKE;
  if(base==E_SPIDER&&o->ambush==AMB_DORMANT)o->ambush=AMB_DESCEND;
  anim(o,A_HIT,"hit","idle",4096,false); say(o,SND_HURT);
}
void enemy_defeat(Object *o){
  if(o->health<=0&&o->ai==A_DEATH)return;
  const int base=kind_of(o); o->health=0;o->pending=false;o->enabled=true;
  fx dur=anim(o,A_DEATH,"death","die",4096,false);
  o->bossT=o->animUntil+FX(2);
  if(base==E_WITCH){o->visible=false;o->bossT=gameTime;}
  if(base==E_GHOST){o->bossT=gameTime+FX(5);o->dropFrom=o->y;o->dropTo=o->y;fx mn[3],mx[3]; hull(o,mn,mx); vec3 a={o->x,o->y+FX(1),o->z},b={o->x,o->y-FX(100),o->z}; Trace t; collide_trace(&t,&a,&b,mn,mx,false); if(t.fraction<4096&&!t.startSolid)o->dropTo=t.end.y;}
  (void)dur; ps.kills++; ps.score+=enemy_stat(o)->score;
  vec3 p; enemy_aim(o,&p); fx_reward(&p,enemy_stat(o)->score,false); fx_smoke(&p);
  say(o,SND_DEATH);
  o->afterPending=base==E_BRUTUSB||base==E_BRUTUSM||base==E_MAXJ||base==E_MAXD||base==E_ZOMBIE;
  if(!o->afterPending)game_event(o,EV_AFTERDESTROYCOMMAND,NULL,0);
}
// Runs for defeated enemies, also during a conversation.
void enemy_dying(Object *o){
  if(o->afterPending&&gameTime>=o->animUntil){o->afterPending=false;game_event(o,EV_AFTERDESTROYCOMMAND,NULL,0);}
  if(kind_of(o)==E_GHOST&&o->dropTo!=o->dropFrom&&gameTime<o->bossT){s32 f=divf32(o->bossT-gameTime,FX(5)); o->y=o->dropTo+mulf32(o->dropFrom-o->dropTo,f);}
}
bool enemy_visible(const Object *o){
  const int b=kind_of(o);
  if(o->bossHidden)return false;
  if(b==E_WITCH&&o->health<=0)return false;
  if((b==E_ZOMBIE||b==E_WITCH)&&o->health>0&&!o->enabled)return false;
  return o->health>0||(o->ai==A_DEATH&&gameTime<o->bossT);
}
