// World rendering: PVS rows select grid cells, cells are frustum culled, drawn nearest first until the budget is spent.
#include "game.h"
Camera cam; int statDrawn,statCells,statPolyBudget=1500,statWorldNeed; const u8 *renderRow; s32 camRight[3],camUp[3];
static s32 view[12]; static s32 fr[4][3];   // frustum plane normals in 4.12
#define FOV_VERT 54   // the original's projection on a 4:3 screen
#define BUCKETS 12
#define MAX_PER_BUCKET 64
static u16 bucket[BUCKETS][MAX_PER_BUCKET]; static u8 bucketN[BUCKETS];
void render_init(void){
  videoSetMode(MODE_0_3D); vramSetBankF(VRAM_F_LCD);
  glInit(); glEnable(GL_TEXTURE_2D|GL_ANTIALIAS|GL_FOG|GL_BLEND); glDisable(GL_ANTIALIAS);
  glClearColor(0,0,0,31); glClearPolyID(63); glClearDepth(GL_MAX_DEPTH);
  glViewport(0,0,255,191);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(FOV_VERT,256.0f/192,0.125f,38.0f);
  glMatrixMode(GL_MODELVIEW);
  // Fog hides the draw distance. Depth is w in 1/4096 gl units (64 world units); table steps of 2 gl units.
  glFogShift(6); 
  GFX_FOG_OFFSET=(unsigned short)(0x7fff&(10*4096>>9)*1);
  for(int i=0;i<32;i++){int a=i*31/12; GFX_FOG_TABLE[i]=a>127?127:(a*4>127?127:a*4);}
}
void render_level_ready(void){
  u32 c=level.h->fog; glClearColor(c&31,(c>>5)&31,(c>>10)&31,31); glFogColor(c&31,(c>>5)&31,(c>>10)&31,31); renderRow=NULL;
}
void render_view(void){
  int sy=sinLerp(cam.yaw),cy=cosLerp(cam.yaw),sp=sinLerp(cam.pitch),cp=cosLerp(cam.pitch);
  // Camera looks along -Z at yaw 0. Right, up and back vectors in 4.12.
  s32 rx=cy,rz=-sy;
  s32 fx_=-mulf32(sy,cp),fy=sp,fz=-mulf32(cy,cp);
  s32 ux=mulf32(sy,sp),uy=cp,uz=mulf32(cy,sp);
  s32 bx=-fx_,by=-fy,bz=-fz;
  camRight[0]=rx;camRight[1]=0;camRight[2]=rz;camUp[0]=ux;camUp[1]=uy;camUp[2]=uz;
  view[0]=rx;view[1]=ux;view[2]=bx;
  view[3]=0; view[4]=uy; view[5]=by;
  view[6]=rz;view[7]=uz;view[8]=bz;
  // translation = -R*pos, position in gl units (world/64)
  s32 px=cam.pos.x/64,py=cam.pos.y/64,pz=cam.pos.z/64;
  view[9]=-(mulf32(rx,px)+mulf32(rz,pz));
  view[10]=-(mulf32(ux,px)+mulf32(uy,py)+mulf32(uz,pz));
  view[11]=-(mulf32(bx,px)+mulf32(by,py)+mulf32(bz,pz));
  MATRIX_CONTROL=GL_MODELVIEW;
  for(int i=0;i<12;i++) MATRIX_LOAD4x3=view[i];
  MATRIX_STORE=1;
  // Frustum planes pointing inward; half angles with a small margin for the grid cell tests.
  int ha=(int)(0.68f*32768/6.28318f*1.0f*1.25f), va=(int)(0.56f*32768/6.28318f*1.15f);
  s32 sh=sinLerp(ha),ch=cosLerp(ha),sv=sinLerp(va),cv=cosLerp(va);
  for(int a=0;a<3;a++){
    s32 r=a==0?rx:a==1?0:rz,u=a==0?ux:a==1?uy:uz,f=a==0?fx_:a==1?fy:fz;
    fr[0][a]=mulf32(r,ch)+mulf32(f,sh);   // left
    fr[1][a]=-mulf32(r,ch)+mulf32(f,sh);  // right
    fr[2][a]=mulf32(u,cv)+mulf32(f,sv);   // bottom
    fr[3][a]=-mulf32(u,cv)+mulf32(f,sv);  // top
  }
}
static inline void send_list(const u32 *list,u32 words){
  if(!words)return;  // a DMA count of 0 would mean the maximum transfer
  while(DMA_CR(0)&DMA_BUSY);
  DMA_SRC(0)=(u32)list; DMA_DEST(0)=(u32)&GFX_FIFO; DMA_CR(0)=DMA_FIFO|words;
}
// Picks the cells in view, so the actors know how much of the polygon limit the world leaves them.
ITCM_CODE void render_cull(void){
  int leaf=collide_leaf(&cam.pos); int cl=level.leaves[leaf].cluster;
  // A camera outside the visibility data sees every cell in range, as in the main port.
  const u8 *row=cl>=0?level.rows+level.clusterRow[cl]*level.h->stride:NULL; renderRow=row; statWorldNeed=0;
  int cx=cam.pos.x>>12,cy=cam.pos.y>>12,cz=cam.pos.z>>12;
  for(int i=0;i<BUCKETS;i++)bucketN[i]=0;
  u32 n=level.h->ncells; const Cell *cells=level.cells;
  for(u32 i=0;i<n;i+=8){
    u32 bits=row?row[i>>3]:0xFF; if(!bits)continue;
    for(int b=0;bits;b++,bits>>=1){
      if(!(bits&1))continue;
      const Cell *c=&cells[i+b];
      int dx=cx<c->min[0]?c->min[0]-cx:cx>c->max[0]?cx-c->max[0]:0;
      int dy=cy<c->min[1]?c->min[1]-cy:cy>c->max[1]?cy-c->max[1]:0;
      int dz=cz<c->min[2]?c->min[2]-cz:cz>c->max[2]?cz-c->max[2]:0;
      int md=dx>dz?dx+dz/2:dz+dx/2; md=md>dy?md+dy/2:dy+md/2;  // cheap distance
      if(md>DRAW_DISTANCE)continue;
      bool out=false;
      for(int p=0;p<4&&!out;p++){
        const s32 *f=fr[p];
        int x=(f[0]>0?c->max[0]:c->min[0])-cx,y=(f[1]>0?c->max[1]:c->min[1])-cy,z=(f[2]>0?c->max[2]:c->min[2])-cz;
        if(f[0]*x+f[1]*y+f[2]*z<0)out=true;
      }
      if(out)continue;
      int k=md>>7; if(k>=BUCKETS)k=BUCKETS-1;
      if(bucketN[k]<MAX_PER_BUCKET){bucket[k][bucketN[k]++]=i+b;statWorldNeed+=c->polys;}
    }
  }
}
ITCM_CODE void render_world(void){
  int cx=cam.pos.x>>12,cy=cam.pos.y>>12,cz=cam.pos.z>>12; const Cell *cells=level.cells;
  int polys=0,drawn=0;
  for(int k=0;k<BUCKETS;k++){
    for(int j=0;j<bucketN[k];j++){
      const Cell *c=&cells[bucket[k][j]];
      if(polys+c->polys>statPolyBudget){ if(k>2)goto done; continue; }
      send_list(level.lists+c->list/4,c->words);
      polys+=c->polys;drawn++;
    }
  }
done:
  while(DMA_CR(0)&DMA_BUSY);
  // Moving brush models: doors, platforms and puzzle parts.
  for(u32 m=1;m<level.h->nmodels;m++){
    const BrushModel *bm=&level.models[m]; const ModelState *ms=&modelState[m];
    if(!bm->polys||!ms->visible)continue;
    if(polys+bm->polys>statPolyBudget+60)continue;
    int wx=bm->origin[0]+(ms->t[0]>>12),wy=bm->origin[1]+(ms->t[1]>>12),wz=bm->origin[2]+(ms->t[2]>>12);
    int dx=wx-cx,dy=wy-cy,dz=wz-cz; if(abs(dx)>DRAW_DISTANCE||abs(dy)>DRAW_DISTANCE||abs(dz)>DRAW_DISTANCE)continue;
    int rr=(bm->max[0]-bm->min[0]+bm->max[1]-bm->min[1]+bm->max[2]-bm->min[2])/2+60; bool out=false;
    for(int p=0;p<4&&!out;p++){const s32 *f=fr[p]; if(((f[0]*dx+f[1]*dy+f[2]*dz)>>12)<-rr)out=true;}
    if(out)continue;
    MATRIX_RESTORE=1;
    if(ms->rotated){
      MATRIX_TRANSLATE=(ms->pivot[0]+ms->t[0])/64;MATRIX_TRANSLATE=(ms->pivot[1]+ms->t[1])/64;MATRIX_TRANSLATE=(ms->pivot[2]+ms->t[2])/64;
      const s16 *r=ms->rot; MATRIX_MULT3x3=r[0];MATRIX_MULT3x3=r[3];MATRIX_MULT3x3=r[6];MATRIX_MULT3x3=r[1];MATRIX_MULT3x3=r[4];MATRIX_MULT3x3=r[7];MATRIX_MULT3x3=r[2];MATRIX_MULT3x3=r[5];MATRIX_MULT3x3=r[8];
      MATRIX_TRANSLATE=(((fx)bm->origin[0]<<12)-ms->pivot[0])/64;MATRIX_TRANSLATE=(((fx)bm->origin[1]<<12)-ms->pivot[1])/64;MATRIX_TRANSLATE=(((fx)bm->origin[2]<<12)-ms->pivot[2])/64;
    } else {
      MATRIX_TRANSLATE=(((fx)bm->origin[0]<<12)+ms->t[0])/64;MATRIX_TRANSLATE=(((fx)bm->origin[1]<<12)+ms->t[1])/64;MATRIX_TRANSLATE=(((fx)bm->origin[2]<<12)+ms->t[2])/64;
    }
    send_list(level.lists+bm->list/4,bm->words); polys+=bm->polys;
  }
  while(DMA_CR(0)&DMA_BUSY);
  statDrawn=polys;statCells=drawn;
}
// Sky cube around the camera. Its faces lie behind the draw distance, so the world always covers them.
void render_sky(void){
  static const s8 faces[6][4][3]={
    {{1,1,1},{1,1,-1},{1,-1,-1},{1,-1,1}},{{-1,1,-1},{-1,1,1},{-1,-1,1},{-1,-1,-1}},
    {{-1,1,-1},{1,1,-1},{1,1,1},{-1,1,1}},{{-1,-1,1},{1,-1,1},{1,-1,-1},{-1,-1,-1}},
    {{-1,1,1},{1,1,1},{1,-1,1},{-1,-1,1}},{{1,1,-1},{-1,1,-1},{-1,-1,-1},{1,-1,-1}}};
  static const u8 order[6]={2,3,0,1,5,4};
  MATRIX_RESTORE=1;
  MATRIX_TRANSLATE=cam.pos.x/64;MATRIX_TRANSLATE=cam.pos.y/64;MATRIX_TRANSLATE=cam.pos.z/64;
  MATRIX_SCALE=3<<12;MATRIX_SCALE=3<<12;MATRIX_SCALE=3<<12;
  GFX_COLOR=0x7FFF;
  const s16 h=7<<12;
  for(int f=0;f<6;f++){
    u32 param=level.sky[order[f]*2],pal=level.sky[order[f]*2+1]; if(!param)continue;
    int w=8<<((param>>20)&7),ht=8<<((param>>23)&7);
    GFX_TEX_FORMAT=param; GFX_PAL_FORMAT=pal; GFX_POLY_FORMAT=(31<<16)|(3<<6)|(5<<24);
    GFX_BEGIN=GL_QUADS;
    static const u8 uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;i++){
      GFX_TEX_COORD=TEXTURE_PACK((uv[i][0]*w)<<4,(uv[i][1]*ht)<<4);
      const s8 *v=faces[f][i];
      GFX_VERTEX16=VERTEX_PACK(v[0]*h,v[1]*h); GFX_VERTEX16=(u16)(v[2]*h);
    }
    GFX_END=0;
  }
}
// A camera-facing quad. Size is in world units; soft sprites use the A5I3 textures from the level file.
void render_billboard(const vec3 *pos,int size,int sprite,u16 color,int alpha,int id,int angle){
  u32 param=level.sprite[sprite*2],pal=level.sprite[sprite*2+1]; if(!param)return;
  int tw=(8<<((param>>20)&7))<<4;
  MATRIX_RESTORE=1; MATRIX_TRANSLATE=pos->x/64;MATRIX_TRANSLATE=pos->y/64;MATRIX_TRANSLATE=pos->z/64;
  s32 h=size*32; // half size in 1/64 world units is raw 4.12 gl units
  s32 ca=cosLerp(angle),sa=sinLerp(angle);
  s32 ax=(camRight[0]*ca+camUp[0]*sa)>>12,ay=(camRight[1]*ca+camUp[1]*sa)>>12,az=(camRight[2]*ca+camUp[2]*sa)>>12;
  s32 bx=(camUp[0]*ca-camRight[0]*sa)>>12,by=(camUp[1]*ca-camRight[1]*sa)>>12,bz=(camUp[2]*ca-camRight[2]*sa)>>12;
  ax=(ax*h)>>12;ay=(ay*h)>>12;az=(az*h)>>12;bx=(bx*h)>>12;by=(by*h)>>12;bz=(bz*h)>>12;
  GFX_TEX_FORMAT=param; GFX_PAL_FORMAT=pal; GFX_POLY_FORMAT=(alpha<<16)|(3<<6)|((u32)(id&63)<<24);
  GFX_COLOR=color; GFX_BEGIN=GL_QUADS;
  GFX_TEX_COORD=TEXTURE_PACK(0,0);  GFX_VERTEX16=VERTEX_PACK(bx-ax,by-ay);GFX_VERTEX16=(u16)(bz-az);
  GFX_TEX_COORD=TEXTURE_PACK(tw,0); GFX_VERTEX16=VERTEX_PACK(bx+ax,by+ay);GFX_VERTEX16=(u16)(bz+az);
  GFX_TEX_COORD=TEXTURE_PACK(tw,tw);GFX_VERTEX16=VERTEX_PACK(ax-bx,ay-by);GFX_VERTEX16=(u16)(az-bz);
  GFX_TEX_COORD=TEXTURE_PACK(0,tw); GFX_VERTEX16=VERTEX_PACK(-ax-bx,-ay-by);GFX_VERTEX16=(u16)(-az-bz);
  GFX_END=0;
}
// A sprite lying flat on the floor, for RedCat's shadow.
void render_flat(const vec3 *pos,int half,int sprite,u16 color,int alpha,int id){
  u32 param=level.sprite[sprite*2],pal=level.sprite[sprite*2+1]; if(!param)return;
  int tw=(8<<((param>>20)&7))<<4; s32 h=half*64;
  MATRIX_RESTORE=1; MATRIX_TRANSLATE=pos->x/64;MATRIX_TRANSLATE=pos->y/64;MATRIX_TRANSLATE=pos->z/64;
  GFX_TEX_FORMAT=param; GFX_PAL_FORMAT=pal; GFX_POLY_FORMAT=(alpha<<16)|(3<<6)|((u32)(id&63)<<24);
  GFX_COLOR=color; GFX_BEGIN=GL_QUADS;
  GFX_TEX_COORD=TEXTURE_PACK(0,0);  GFX_VERTEX16=VERTEX_PACK(-h,0);GFX_VERTEX16=(u16)(-h);
  GFX_TEX_COORD=TEXTURE_PACK(tw,0); GFX_VERTEX16=VERTEX_PACK(h,0);GFX_VERTEX16=(u16)(-h);
  GFX_TEX_COORD=TEXTURE_PACK(tw,tw);GFX_VERTEX16=VERTEX_PACK(h,0);GFX_VERTEX16=(u16)(h);
  GFX_TEX_COORD=TEXTURE_PACK(0,tw); GFX_VERTEX16=VERTEX_PACK(-h,0);GFX_VERTEX16=(u16)(h);
  GFX_END=0;
}
// A quad between two points that turns towards the camera around its length, for beams.
void render_strip(const vec3 *a,const vec3 *b,int width,int sprite,u16 color,int alpha,int id){
  u32 param=level.sprite[sprite*2],pal=level.sprite[sprite*2+1]; if(!param)return;
  int tw=(8<<((param>>20)&7))<<4;
  int dx=(b->x-a->x)>>6,dy=(b->y-a->y)>>6,dz=(b->z-a->z)>>6;   // 1/64 units of 1/64 world units: gl 4.12 after the shift below
  int cx=(a->x-cam.pos.x)>>12,cy=(a->y-cam.pos.y)>>12,cz=(a->z-cam.pos.z)>>12;
  s32 sx=(dy>>6)*cz-(dz>>6)*cy,sy=(dz>>6)*cx-(dx>>6)*cz,sz=(dx>>6)*cy-(dy>>6)*cx;
  while(abs(sx)>20000||abs(sy)>20000||abs(sz)>20000){sx>>=4;sy>>=4;sz>>=4;}
  s32 l=sqrt32(sx*sx+sy*sy+sz*sz); if(!l)return;
  s32 w=width*32; sx=sx*w/l;sy=sy*w/l;sz=sz*w/l;
  if(abs(dx)>30000||abs(dy)>30000||abs(dz)>30000)return;  // beyond the 4.12 vertex range of 8 gl units (512 world units) is split by the caller
  MATRIX_RESTORE=1; MATRIX_TRANSLATE=a->x/64;MATRIX_TRANSLATE=a->y/64;MATRIX_TRANSLATE=a->z/64;
  GFX_TEX_FORMAT=param; GFX_PAL_FORMAT=pal; GFX_POLY_FORMAT=(alpha<<16)|(3<<6)|((u32)(id&63)<<24);
  GFX_COLOR=color; GFX_BEGIN=GL_QUADS;
  GFX_TEX_COORD=TEXTURE_PACK(0,0);  GFX_VERTEX16=VERTEX_PACK(-sx,-sy);GFX_VERTEX16=(u16)(-sz);
  GFX_TEX_COORD=TEXTURE_PACK(tw,0); GFX_VERTEX16=VERTEX_PACK(sx,sy);GFX_VERTEX16=(u16)(sz);
  GFX_TEX_COORD=TEXTURE_PACK(tw,tw);GFX_VERTEX16=VERTEX_PACK(dx+sx,dy+sy);GFX_VERTEX16=(u16)(dz+sz);
  GFX_TEX_COORD=TEXTURE_PACK(0,tw); GFX_VERTEX16=VERTEX_PACK(dx-sx,dy-sy);GFX_VERTEX16=(u16)(dz-sz);
  GFX_END=0;
}
void render_finish(void){
  while(DMA_CR(0)&DMA_BUSY);
  GFX_FLUSH=GL_WBUFFERING;
}
