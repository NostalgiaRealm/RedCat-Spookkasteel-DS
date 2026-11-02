// Fixed-point port of the BSP convex leaf sweep used by the original engine and the main port.
#include "game.h"
#include <stdlib.h>
#include <string.h>
#define EPS FX(0.05)
static inline s32 dot30(const s32 *n,fx x,fx y,fx z){return (s32)(((s64)n[0]*x+(s64)n[1]*y+(s64)n[2]*z)>>30);}
static inline s32 mul30(s32 n,fx v){return (s32)(((s64)n*v)>>30);}
static inline s32 fdiv(s32 a,s32 b){return divf32(a,b);}
typedef struct {
  Trace *t; vec3 s,e; fx mins[3],maxs[3]; fx boxMin[3],boxMax[3]; fx c[3],ext[3]; int model; u16 stamp;
} Sweep;
static Sweep sw;
static u16 *seen; static u32 seenSize; static u16 stampCounter;
ITCM_CODE static void sweep_leaf(int index){
  if(seen[index]==sw.stamp)return; seen[index]=sw.stamp;
  const Leaf *leaf=&level.leaves[index];
  if(!(leaf->contents&67)||!leaf->numSides)return;
  const fx *bmn=sw.boxMin,*bmx=sw.boxMax;
  for(int i=0;i<3;i++) if(((fx)leaf->min[i]<<12)>bmx[i]||((fx)leaf->max[i]<<12)<bmn[i])return;
  s32 enter=-0x7fffffff,leave=4096,nearest=-0x7fffffff; const Plane *nPlane=NULL,*xPlane=NULL; int nSign=1,xSign=1; bool outside=false,endOutside=false;
  Trace *t=sw.t;
  for(int j=0;j<leaf->numSides;j++){
    u16 side=level.sides[leaf->firstSide+j]; const Plane *p=&level.planes[side&0x7fff]; int sign=(side&0x8000)?-1:1;
    s32 nx=p->n[0]*sign,ny=p->n[1]*sign,nz=p->n[2]*sign;
    s32 off=mul30(nx,nx>0?sw.mins[0]:sw.maxs[0])+mul30(ny,ny>0?sw.mins[1]:sw.maxs[1])+mul30(nz,nz>0?sw.mins[2]:sw.maxs[2]);
    s32 d=p->d*sign-off;
    s32 sv[3]={nx,ny,nz};
    s32 ds=dot30(sv,sw.s.x,sw.s.y,sw.s.z)-d,de=dot30(sv,sw.e.x,sw.e.y,sw.e.z)-d;
    if(ds>nearest){nearest=ds;xPlane=p;xSign=sign;}
    if(ds>0)outside=true; if(de>0)endOutside=true;
    if(ds>0&&de>=ds)return;
    if(ds<=0&&de<=0)continue;
    if(ds>de){s32 f=fdiv(ds-EPS,ds-de); if(f>enter){enter=f;nPlane=p;nSign=sign;}}
    else {s32 f=fdiv(ds+EPS,ds-de); if(f<leave)leave=f;}
  }
  if(!outside){
    t->startSolid=true;
    {s32 dist=(nearest<0?-nearest:0)+FX(.08); if(xPlane&&(!t->penDist||dist<t->penDist)){t->penDist=dist;t->penNormal[0]=xPlane->n[0]*xSign;t->penNormal[1]=xPlane->n[1]*xSign;t->penNormal[2]=xPlane->n[2]*xSign;}}
    if(!endOutside){t->fraction=0;t->model=sw.model;}
    return;
  }
  if(enter<leave&&enter>-0x7fffffff&&enter<t->fraction){
    t->fraction=enter<0?0:enter;
    t->normal[0]=nPlane->n[0]*nSign;t->normal[1]=nPlane->n[1]*nSign;t->normal[2]=nPlane->n[2]*nSign;t->model=sw.model;
  }
}
ITCM_CODE static void sweep_node(int id){
  while(id>=0){
    const Node *node=&level.nodes[id]; const Plane *p=&level.planes[node->plane];
    s32 d=dot30(p->n,sw.c[0],sw.c[1],sw.c[2])-p->d;
    s32 r=mul30(abs(p->n[0]),sw.ext[0])+mul30(abs(p->n[1]),sw.ext[1])+mul30(abs(p->n[2]),sw.ext[2]);
    bool front=d+r>=0,back=d-r<=0;
    if(front&&back){sweep_node(node->c0);id=node->c1;}
    else if(front)id=node->c0; else if(back)id=node->c1; else return;
  }
  sweep_leaf(-id-1);
}
static void sweep_model(int m){
  const BrushModel *bm=&level.models[m];
  sw.model=m; sw.stamp=++stampCounter;
  if(stampCounter==0xffff){memset(seen,0,seenSize);stampCounter=0;}
  sweep_node(bm->root);
}
static void to_local(const ModelState *ms,vec3 *v){
  fx ox=ms->t[0],oy=ms->t[1],oz=ms->t[2];
  if(ms->rotated){
    fx px=v->x-ms->pivot[0]-ox,py=v->y-ms->pivot[1]-oy,pz=v->z-ms->pivot[2]-oz;
    v->x=ms->pivot[0]+(s32)(((s64)ms->rot[0]*px+(s64)ms->rot[3]*py+(s64)ms->rot[6]*pz)>>12);
    v->y=ms->pivot[1]+(s32)(((s64)ms->rot[1]*px+(s64)ms->rot[4]*py+(s64)ms->rot[7]*pz)>>12);
    v->z=ms->pivot[2]+(s32)(((s64)ms->rot[2]*px+(s64)ms->rot[5]*py+(s64)ms->rot[8]*pz)>>12);
  } else {v->x-=ox;v->y-=oy;v->z-=oz;}
}
static void to_world_dir(const ModelState *ms,s32 *n){
  s32 a=n[0],b=n[1],c=n[2];
  n[0]=(s32)(((s64)ms->rot[0]*a+(s64)ms->rot[1]*b+(s64)ms->rot[2]*c)>>12);
  n[1]=(s32)(((s64)ms->rot[3]*a+(s64)ms->rot[4]*b+(s64)ms->rot[5]*c)>>12);
  n[2]=(s32)(((s64)ms->rot[6]*a+(s64)ms->rot[7]*b+(s64)ms->rot[8]*c)>>12);
}
// Moving brushes are swept in model space, with the player's box kept axis aligned.
static void sweep_moved(int m,const vec3 *s,const vec3 *e,const fx *mins,const fx *maxs){
  const ModelState *ms=&modelState[m]; const BrushModel *bm=&level.models[m]; Trace *t=sw.t;
  vec3 ls=*s,le=*e; to_local(ms,&ls); to_local(ms,&le);
  fx lmn[3],lmx[3],lc[3]={ls.x,ls.y,ls.z},ld[3]={le.x,le.y,le.z};
  for(int a=0;a<3;a++){lmn[a]=(lc[a]<ld[a]?lc[a]:ld[a])+mins[a];lmx[a]=(lc[a]>ld[a]?lc[a]:ld[a])+maxs[a]; if(((fx)bm->min[a]<<12)>lmx[a]||((fx)bm->max[a]<<12)<lmn[a])return;}
  fx bmn[3],bmx[3],cc[3],ee[3]; memcpy(bmn,sw.boxMin,12);memcpy(bmx,sw.boxMax,12);memcpy(cc,sw.c,12);memcpy(ee,sw.ext,12);
  sw.s=ls;sw.e=le;
  for(int a=0;a<3;a++){sw.boxMin[a]=lmn[a];sw.boxMax[a]=lmx[a];sw.c[a]=(lmn[a]+lmx[a])/2;sw.ext[a]=(lmx[a]-lmn[a])/2+1;}
  s32 before=t->fraction,penBefore=t->penDist; sweep_model(m);
  if(ms->rotated){ if(t->fraction<before)to_world_dir(ms,t->normal); if(t->penDist!=penBefore)to_world_dir(ms,t->penNormal); }
  memcpy(sw.boxMin,bmn,12);memcpy(sw.boxMax,bmx,12);memcpy(sw.c,cc,12);memcpy(sw.ext,ee,12);sw.s=*s;sw.e=*e;
}
static void begin(Trace *t,const vec3 *start,const vec3 *end,const fx *mins,const fx *maxs){
  if(seenSize!=level.h->nleaves*2){free(seen); seenSize=level.h->nleaves*2; seen=malloc(seenSize); memset(seen,0,seenSize); stampCounter=0;}
  t->fraction=4096;t->startSolid=false;t->model=-1;t->normal[0]=0;t->normal[1]=1<<30;t->normal[2]=0;t->penDist=0;
  sw.t=t;sw.s=*start;sw.e=*end;
  fx sc[3]={start->x,start->y,start->z},ec[3]={end->x,end->y,end->z};
  for(int i=0;i<3;i++){
    sw.mins[i]=mins[i];sw.maxs[i]=maxs[i];
    sw.boxMin[i]=(sc[i]<ec[i]?sc[i]:ec[i])+mins[i]; sw.boxMax[i]=(sc[i]>ec[i]?sc[i]:ec[i])+maxs[i];
    sw.c[i]=(sw.boxMin[i]+sw.boxMax[i])/2; sw.ext[i]=(sw.boxMax[i]-sw.boxMin[i])/2+1;
  }
}
static void finish(Trace *t,const vec3 *start,const vec3 *end){
  s32 f=t->fraction;
  t->end.x=start->x+(s32)(((s64)(end->x-start->x)*f)>>12);
  t->end.y=start->y+(s32)(((s64)(end->y-start->y)*f)>>12);
  t->end.z=start->z+(s32)(((s64)(end->z-start->z)*f)>>12);
}
void collide_trace_skip(Trace *t,const vec3 *start,const vec3 *end,const fx *mins,const fx *maxs,int skip){
  begin(t,start,end,mins,maxs);
  sweep_model(0);
  for(u32 m=1;m<level.h->nmodels;m++) if(modelState[m].solid&&(int)m!=skip)sweep_moved(m,start,end,mins,maxs);
  finish(t,start,end);
}
void collide_trace(Trace *t,const vec3 *start,const vec3 *end,const fx *mins,const fx *maxs,bool withModels){
  if(withModels){collide_trace_skip(t,start,end,mins,maxs,-1);return;}
  begin(t,start,end,mins,maxs); sweep_model(0); finish(t,start,end);
}
void collide_trace_only(Trace *t,const vec3 *start,const vec3 *end,const fx *mins,const fx *maxs,int model){
  begin(t,start,end,mins,maxs); sweep_moved(model,start,end,mins,maxs); finish(t,start,end);
}
// Contents of the BSP cells a box touches in one model, for trigger and liquid volumes.
static u32 box_contents(int id,const fx *c,const fx *ext){
  u32 out=0;
  while(id>=0){
    const Node *node=&level.nodes[id]; const Plane *p=&level.planes[node->plane];
    s32 d=dot30(p->n,c[0],c[1],c[2])-p->d;
    s32 r=mul30(abs(p->n[0]),ext[0])+mul30(abs(p->n[1]),ext[1])+mul30(abs(p->n[2]),ext[2]);
    bool front=d+r>=0,back=d-r<0;
    if(front&&back){out|=box_contents(node->c0,c,ext);id=node->c1;}
    else if(front)id=node->c0; else id=node->c1;
  }
  return out|level.leaves[-id-1].contents;
}
u32 collide_box_contents(int model,const vec3 *p,const fx *mins,const fx *maxs){
  vec3 q=*p; if(model)to_local(&modelState[model],&q);
  fx c[3]={q.x+(mins[0]+maxs[0])/2,q.y+(mins[1]+maxs[1])/2,q.z+(mins[2]+maxs[2])/2},e[3]={(maxs[0]-mins[0])/2,(maxs[1]-mins[1])/2,(maxs[2]-mins[2])/2};
  return box_contents(level.models[model].root,c,e);
}
int collide_leaf(const vec3 *p){
  int id=level.models[0].root;
  while(id>=0){const Node *n=&level.nodes[id];const Plane *pl=&level.planes[n->plane];id=dot30(pl->n,p->x,p->y,p->z)-pl->d>=0?n->c0:n->c1;}
  return -id-1;
}
