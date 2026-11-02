#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
Level level;
ModelState *modelState;
static void *read_file(const char *name,size_t *size){
  FILE *f=fopen(name,"rb"); if(!f)return NULL;
  fseek(f,0,SEEK_END); size_t n=ftell(f); fseek(f,0,SEEK_SET);
  void *p=malloc(n); if(p)fread(p,1,n,f); fclose(f); if(size)*size=n; return p;
}
bool level_load(int index){
  char name[32];
  free(level.base); free(modelState); level.base=NULL; modelState=NULL;
  sprintf(name,"nitro:/l%d.tex",index);
  size_t n; u32 *tex=read_file(name,&n); if(!tex)return false;
  u32 vs=tex[0],ps=tex[1]; const u8 *px=(const u8*)(tex+2);
  DC_FlushAll();
  // Textures use banks A, B and D as slots 0 to 2, 128 KiB each.
  vramSetBankA(VRAM_A_LCD); vramSetBankB(VRAM_B_LCD); vramSetBankD(VRAM_D_LCD); vramSetBankE(VRAM_E_LCD);
  for(u32 o=0;o<vs;o+=128*1024){u32 n2=vs-o>128*1024?128*1024:vs-o; u16 *dst=o==0?VRAM_A:o==128*1024?VRAM_B:VRAM_D; dmaCopyWords(3,px+o,dst,(n2+3)&~3);}
  dmaCopyWords(3,px+vs,VRAM_E,ps);
  vramSetBankA(VRAM_A_TEXTURE_SLOT0); vramSetBankB(VRAM_B_TEXTURE_SLOT1); vramSetBankD(VRAM_D_TEXTURE_SLOT2); vramSetBankE(VRAM_E_TEX_PALETTE);
  memcpy(level.sky,px+vs+ps,sizeof level.sky+sizeof level.sprite);
  free(tex);
  sprintf(name,"nitro:/l%d.bin",index);
  u8 *b=read_file(name,&n); if(!b)return false;
  DC_FlushAll();
  LevelHeader *h=(LevelHeader*)b; level.h=h; level.base=b;
  level.lists=(const u32*)(b+h->oLists); level.cells=(const Cell*)(b+h->oCells); level.clusterRow=(const u16*)(b+h->oClusterRow);
  level.rows=b+h->oRows; level.planes=(const Plane*)(b+h->oPlanes); level.nodes=(const Node*)(b+h->oNodes);
  level.leaves=(const Leaf*)(b+h->oLeaves); level.sides=(const u16*)(b+h->oSides); level.models=(const BrushModel*)(b+h->oModels);
  modelState=calloc(h->nmodels,sizeof(ModelState));
  for(u32 i=1;i<h->nmodels;i++){modelState[i].solid=false; modelState[i].visible=false; modelState[i].rot[0]=modelState[i].rot[4]=modelState[i].rot[8]=4096;}
  return true;
}
