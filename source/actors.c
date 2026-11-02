// Actor rendering. Bones are stored in the hardware matrix stack, so vertices need no CPU skinning.
#include "game.h"
#include <string.h>
u16 light_at(const vec3 *p){
  int i=((p->x>>12)-gd.glo[0])>>8,j=((p->y>>12)-gd.glo[1])>>8,k=((p->z>>12)-gd.glo[2])>>8;
  if(i<0)i=0;if(j<0)j=0;if(k<0)k=0;if(i>=gd.gn[0])i=gd.gn[0]-1;if(j>=gd.gn[1])j=gd.gn[1]-1;if(k>=gd.gn[2])k=gd.gn[2]-1;
  return gd.grid[(i*gd.gn[1]+j)*gd.gn[2]+k];
}
int model_clip(int model,const char *name){
  const ModelDef *m=&gd.models[model];
  for(int i=0;i<m->nclips;i++) if(!strncmp(gd.clips[m->clip+i].name,name,8))return i;
  return -1;
}
static inline void send(const u32 *list,u32 words){
  if(!words)return;  // a DMA count of 0 would mean the maximum transfer
  while(DMA_CR(0)&DMA_BUSY);
  DMA_SRC(0)=(u32)list; DMA_DEST(0)=(u32)&GFX_FIFO; DMA_CR(0)=DMA_FIFO|words;
  while(DMA_CR(0)&DMA_BUSY);
}
// Returns the polygons used. Order matches the main port: support model, spin, then Z*Y*X, then the object's scale.
ITCM_CODE int actor_draw(int model,bool far,const vec3 *pos,const s16 *modelRot,fx age,int yaw,int pitchX,int rollZ,u16 scale,int clip,fx time,bool loop,u16 color){
  const ModelDef *m=&gd.models[model];
  MATRIX_RESTORE=1;
  MATRIX_TRANSLATE=pos->x/64; MATRIX_TRANSLATE=pos->y/64; MATRIX_TRANSLATE=pos->z/64;
  if(modelRot){const s16 *r=modelRot; MATRIX_MULT3x3=r[0];MATRIX_MULT3x3=r[3];MATRIX_MULT3x3=r[6];MATRIX_MULT3x3=r[1];MATRIX_MULT3x3=r[4];MATRIX_MULT3x3=r[7];MATRIX_MULT3x3=r[2];MATRIX_MULT3x3=r[5];MATRIX_MULT3x3=r[8];}
  if(age){const s16 *sp=m->spin; // the model's own turning, Rz*Ry*Rx
    if(sp[2]){s32 a=(s32)(((s64)age*sp[2])>>10),s=sinLerp(a),c=cosLerp(a);
      MATRIX_MULT3x3=c;MATRIX_MULT3x3=s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=-s;MATRIX_MULT3x3=c;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=4096;}
    if(sp[1]){s32 a=(s32)(((s64)age*sp[1])>>10),s=sinLerp(a),c=cosLerp(a);
      MATRIX_MULT3x3=c;MATRIX_MULT3x3=0;MATRIX_MULT3x3=-s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=4096;MATRIX_MULT3x3=0;MATRIX_MULT3x3=s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=c;}
    if(sp[0]){s32 a=(s32)(((s64)age*sp[0])>>10),s=sinLerp(a),c=cosLerp(a);
      MATRIX_MULT3x3=4096;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=c;MATRIX_MULT3x3=s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=-s;MATRIX_MULT3x3=c;}
  }
  if(rollZ){s32 s=sinLerp(rollZ),c=cosLerp(rollZ);
    MATRIX_MULT3x3=c;MATRIX_MULT3x3=s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=-s;MATRIX_MULT3x3=c;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=4096;}
  if(yaw){s32 s=sinLerp(yaw),c=cosLerp(yaw);
    MATRIX_MULT3x3=c;MATRIX_MULT3x3=0;MATRIX_MULT3x3=-s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=4096;MATRIX_MULT3x3=0;MATRIX_MULT3x3=s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=c;}
  if(pitchX){s32 s=sinLerp(pitchX),c=cosLerp(pitchX);
    MATRIX_MULT3x3=4096;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=0;MATRIX_MULT3x3=c;MATRIX_MULT3x3=s;MATRIX_MULT3x3=0;MATRIX_MULT3x3=-s;MATRIX_MULT3x3=c;}
  if(scale!=256){s32 f=scale<<4; MATRIX_SCALE=f;MATRIX_SCALE=f;MATRIX_SCALE=f;}
  {int r=color&31,g=(color>>5)&31,b=(color>>10)&31;
   u16 amb=RGB15(r*5/8,g*5/8,b*5/8),dif=RGB15(r*7/8,g*7/8,b*7/8);
   GFX_DIFFUSE_AMBIENT=dif|(amb<<16); GFX_SPECULAR_EMISSION=0;}
  if(m->bones&&m->nclips){
    if(clip<0||clip>=m->nclips)clip=0;
    const ClipDef *c=&gd.clips[m->clip+clip];
    int nf=c->frames; s32 t=time*c->rate; int f0=t>>12; s32 w=t&4095;
    if(loop){f0%=nf;} else if(f0>=nf-1){f0=nf-1;w=0;}
    int f1=f0+1; if(f1>=nf)f1=loop?0:nf-1;
    const s16 *a=(const s16*)(gd.frames+c->data+f0*m->bones*24),*b=(const s16*)(gd.frames+c->data+f1*m->bones*24);
    MATRIX_STORE=2;
    for(int i=0;i<m->bones;i++){
      MATRIX_RESTORE=2;
      for(int k=0;k<12;k++){s32 v=a[k]+(((b[k]-a[k])*w)>>12); MATRIX_MULT4x3=v;}
      MATRIX_STORE=3+i;
      a+=12;b+=12;
    }
  }
  if(far){send(gd.modelLists+m->listFar/4,m->wordsFar);return m->polysFar;}
  send(gd.modelLists+m->list/4,m->words);
  return m->polys;
}
