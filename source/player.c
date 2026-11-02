#include "game.h"
Player player; bool cameraHidesPlayer;
const fx PLAYER_MINS[3]={-FX(12.48),0,-FX(12.48)},PLAYER_MAXS[3]={FX(12.48),FX(57.92),FX(12.48)};
static const fx HULL_MIN[3]={-FX(8),-FX(8),-FX(8)},HULL_MAX[3]={FX(8),FX(8),FX(8)};
#define GRAVITY FX(800)
#define JUMP_SPEED FX(258)
#define STEP FX(16)
#define FLOOR_Y ((s32)(0.7*(1<<30)))
static int camYawSmooth;
// atan2 in binary angles (32768 per turn).
int atan2_bin(s32 y,s32 x){
  if(!x&&!y)return 0;
  s32 ax=abs(x),ay=abs(y); s32 mn=ax<ay?ax:ay,mx=ax<ay?ay:ax;
  s32 a=divf32(mn,mx); s32 r=mulf32(a,FX(0.125))+mulf32(mulf32(a,4096-a),FX(0.04)); // fraction of a turn up to 1/8
  if(ay>ax)r=FX(0.25)-r; if(x<0)r=FX(0.5)-r; if(y<0)r=-r;
  return (r<<3)&0x7fff;
}
static void add_contact(int m){
  if(m<=0)return; for(int i=0;i<player.ncontacts;i++)if(player.contacts[i]==m)return;
  if(player.ncontacts<8)player.contacts[player.ncontacts++]=m;
}
void player_spawn(void){
  const s32 *s=level.h->spawn; player=(Player){.pos={s[0],s[1]+4096,s[2]}};
  player.jumpAge=FX(100);
  player.yaw=(-level.h->yaw*32768/12)&0x7fff; camera_set_yaw(player.yaw); (void)camYawSmooth;
  Trace t; vec3 e=player.pos; e.y-=FX(180); collide_trace(&t,&player.pos,&e,PLAYER_MINS,PLAYER_MAXS,false); if(t.fraction<4096)player.pos=t.end;
}
static void slide(vec3 *pos,fx dx,fx dz,bool *hit){
  fx r[3]={dx,0,dz};
  for(int i=0;i<4;i++){
    if(abs(r[0])+abs(r[1])+abs(r[2])<8)break;
    vec3 e={pos->x+r[0],pos->y+r[1],pos->z+r[2]}; Trace t; collide_trace(&t,pos,&e,PLAYER_MINS,PLAYER_MAXS,true);
    *pos=t.end; if(t.fraction>=4096)break; *hit=true; add_contact(t.model);
    s32 rem=4096-t.fraction; for(int a=0;a<3;a++)r[a]=mulf32(r[a],rem);
    s32 into=(s32)(((s64)r[0]*t.normal[0]+(s64)r[1]*t.normal[1]+(s64)r[2]*t.normal[2])>>30);
    if(into<0)for(int a=0;a<3;a++)r[a]-=(s32)(((s64)t.normal[a]*into)>>30);
    if(t.startSolid&&t.fraction==0)break;
  }
}
// forward/strafe: -4096..4096. Same order of operations as PlayerController.update in the main port.
void player_update(int dtVbl,int forward,int strafe,bool jump,int camYaw){
  if(dtVbl>3)dtVbl=3;
  s32 dt=dtVbl*4096/60;
  bool pressedJump=jump&&!player.prevJump; player.prevJump=jump; player.landed=0; player.jumpAge+=dt;
  player.inputFwd=forward;player.inputSide=strafe;
  int moving=forward||strafe;
  bool ground=player.grounded;
  fx fwdSpeed=ground?(forward>0?FX(4.9*32):FX(4.5*32)):FX(32),sideSpeed=ground?FX(4.5*32):FX(32);
  s32 sy=sinLerp(camYaw),cy=cosLerp(camYaw);
  fx z=mulf32(forward,fwdSpeed),x=mulf32(strafe,sideSpeed);
  if(forward&&strafe){z=mulf32(z,2896);x=mulf32(x,2896);}
  fx ivx=mulf32(-sy,z)+mulf32(cy,x),ivz=mulf32(-cy,z)+mulf32(-sy,x);
  if(moving)player.yaw=camYaw;
  const fx *wind=player.env;
  if(wind[1]>0){player.vel.y=wind[1];player.launch[0]=wind[0];player.launch[1]=wind[2];player.grounded=false;ground=false;}
  bool boost=pressedJump&&!ground&&(ps.skill&8)&&!player.superUsed&&player.jumpAge>=FX(.12)&&player.jumpAge<=FX(.55);
  if(boost){player.vel.y=mulf32(JUMP_SPEED,FX(.89));player.superUsed=true;player.jumpedSuper=true;}
  fx factor=ground?FX(1.4):boost?FX(.4):4096;
  fx dx=mulf32(mulf32(ivx+wind[0]+(ground?0:player.launch[0]),factor),dt),dz=mulf32(mulf32(ivz+wind[2]+(ground?0:player.launch[1]),factor),dt);
  player.ncontacts=0;
  vec3 before=player.pos,moved=before; bool hit=false;
  slide(&moved,dx,dz,&hit);
  if(ground&&hit&&(dx||dz)){
    Trace up; vec3 e=before; e.y+=STEP; collide_trace(&up,&before,&e,PLAYER_MINS,PLAYER_MAXS,true);
    if(up.fraction==4096){
      vec3 st=up.end; bool h2=false; slide(&st,dx,dz,&h2);
      Trace dn; vec3 e2=st; e2.y-=FX(18); collide_trace(&dn,&st,&e2,PLAYER_MINS,PLAYER_MAXS,true);
      if(dn.fraction<4096&&dn.normal[1]>FLOOR_Y){
        s64 a=(s64)(dn.end.x-before.x)*(dn.end.x-before.x)+(s64)(dn.end.z-before.z)*(dn.end.z-before.z);
        s64 b=(s64)(moved.x-before.x)*(moved.x-before.x)+(s64)(moved.z-before.z)*(moved.z-before.z);
        if(a>b)moved=dn.end;
      }
    }
  }
  player.pos=moved;
  if(ground){
    vec3 e=player.pos; e.y-=mulf32(FX(4.9*32),dt); Trace dn; collide_trace(&dn,&player.pos,&e,PLAYER_MINS,PLAYER_MAXS,true);
    player.pos=dn.end; player.grounded=dn.fraction<4096&&dn.normal[1]>FLOOR_Y; if(dn.fraction<4096)add_contact(dn.model);
    if(jump){player.vel.y=JUMP_SPEED;player.launch[0]=ivx+wind[0];player.launch[1]=ivz+wind[2];player.grounded=false;player.jumpAge=0;player.superUsed=false;player.jumped=true;player.jumpedSuper=false;}
    else if(player.grounded){player.vel.y=0;player.launch[0]=player.launch[1]=0;}
    else {player.vel.y=-FX(4.9*32);player.launch[0]=ivx+wind[0];player.launch[1]=ivz+wind[2];}
  } else {
    fx vs=player.vel.y+wind[1]-mulf32(GRAVITY,dt)/2; player.vel.y-=mulf32(GRAVITY,dt);
    vec3 e=player.pos; e.y+=mulf32(vs,dt); Trace f; collide_trace(&f,&player.pos,&e,PLAYER_MINS,PLAYER_MAXS,true);
    player.pos=f.end; player.grounded=false;
    if(f.fraction<4096){
      add_contact(f.model);
      player.grounded=f.normal[1]>FLOOR_Y&&vs<=0;
      if(player.grounded&&!f.startSolid)player.landed=player.vel.y<0?-player.vel.y:0;
      player.vel.y=0;
    }
    if(player.grounded){player.launch[0]=player.launch[1]=0;player.jumpAge=FX(100);player.superUsed=false;player.jumped=false;player.jumpedSuper=false;}
  }
}
static int followYaw,camP=-1365,camLook; static fx camH=FX(1.5*57.92),camD=FX(1.7*57.92); static vec3 camLast;
void camera_set_yaw(int yaw){followYaw=yaw&0x7fff;cam.yaw=followYaw;camLook=0;}
// A locked target turns the view towards it, as the main port's lock-on does.
void camera_turn_toward(int yaw,fx dt){int d=((yaw-followYaw+16384)&0x7fff)-16384; followYaw=(followYaw+(d*(dt>FX(.08)?2048:1400)>>12))&0x7fff;}
// Third person follow camera with a collision sweep from RedCat's head; script cameras take over when active.
void camera_update(int dtVbl,int turn,int look){
  followYaw=(followYaw+turn*dtVbl)&0x7fff; cameraHidesPlayer=false;
  fx elapsed; const CamDef *sc=game_camera(&elapsed);
  if(sc&&sc->mode!=0&&sc->mode!=1){
    // Visit the authored points at 150 units/s, as sampleCameraRoute does; the last leg eases in.
    vec3 p={sc->pos[0],sc->pos[1],sc->pos[2]};
    if(sc->npoints>0){
      const s32 *pts=gd.camPoints+sc->first*3; p=(vec3){pts[0],pts[1],pts[2]};
      fx remaining=elapsed;
      for(int i=1;i<sc->npoints;i++){
        const s32 *a=pts+(i-1)*3,*b=pts+i*3; int ax=(b[0]-a[0])>>8,ay=(b[1]-a[1])>>8,az=(b[2]-a[2])>>8; fx len=sqrt32(ax*ax+ay*ay+az*az)<<8; if(!len)continue;
        fx dur=len/150;
        if(i==sc->npoints-1&&remaining>=dur){p=(vec3){b[0],b[1],b[2]};break;}
        if(remaining<dur){s32 f=divf32(remaining,dur); p=(vec3){a[0]+mulf32(b[0]-a[0],f),a[1]+mulf32(b[1]-a[1],f),a[2]+mulf32(b[2]-a[2],f)};break;}
        remaining-=dur; p=(vec3){b[0],b[1],b[2]};
      }
    }
    vec3 l;
    if(sc->hasTpos)l=(vec3){sc->tpos[0],sc->tpos[1],sc->tpos[2]};
    else if(sc->target>=0)l=(vec3){objects[sc->target].x,objects[sc->target].y,objects[sc->target].z};
    else l=(vec3){player.pos.x,player.pos.y+FX(43),player.pos.z};
    p.x+=sc->offset[0];p.y+=sc->offset[1];p.z+=sc->offset[2];
    fx dx=l.x-p.x,dy=l.y-p.y,dz=l.z-p.z; int hx=dx>>6,hz=dz>>6; fx h=sqrt32(hx*hx+hz*hz)<<6;
    cam.pos=p; cam.yaw=atan2_bin(-dx,-dz)&0x7fff; { int pa=atan2_bin(dy,h); if(pa>16384)pa-=32768; cam.pitch=pa; }
    return;
  }
  cam.yaw=followYaw;
  // The main port's first camera preset: closer and steeper while RedCat stands, further back and level while he moves.
  bool walking=player.pos.x!=camLast.x||player.pos.z!=camLast.z||player.pos.y!=camLast.y; camLast=player.pos;
  fx wantH=walking?FX(1.25*57.92):FX(1.5*57.92),wantD=walking?FX(2.5*57.92):FX(1.7*57.92); int wantP=walking?-546:-1365;
  // Camera mode 1 replaces the preset with its CamOffset: times the player's height up and back, pitch in degrees.
  if(sc&&sc->mode==1){wantH=mulf32(sc->offset[0],FX(57.92));wantD=mulf32(sc->offset[1],FX(57.92));wantP=(sc->offset[2]*91)>>12;}
  camH+=(wantH-camH)*dtVbl/24; camD+=(wantD-camD)*dtVbl/24; camP+=(wantP-camP)*dtVbl/24;
  if(look)camLook+=look*dtVbl; else camLook-=camLook*dtVbl/16;
  if(camLook>3000)camLook=3000; if(camLook<-2000)camLook=-2000;
  cam.pitch=camP+camLook;
  s32 sy=sinLerp(cam.yaw),cy=cosLerp(cam.yaw);
  vec3 head={player.pos.x,player.pos.y+FX(54.08),player.pos.z};
  vec3 want={player.pos.x+mulf32(sy,camD),player.pos.y+camH,player.pos.z+mulf32(cy,camD)};
  Trace t; collide_trace(&t,&head,&want,HULL_MIN,HULL_MAX,true);
  cam.pos=t.end;
  // A wall right behind RedCat would put the view inside him: then he is not drawn, as in the main port.
  {int dx=(cam.pos.x-player.pos.x)>>12,dz=(cam.pos.z-player.pos.z)>>12; cameraHidesPlayer=dx*dx+dz*dz<28*28;}
}
