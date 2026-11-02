// Saving to the card's FAT file system, when there is one (a flash card, or melonDS with DLDI on). Like the main port it
// saves at every save point: the objects, script variables and moving brushes, so an interrupted level resumes as it was.
#include "game.h"
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SAVE_PATH "fat:/redcat_ds.sav"
#define SAVE_MAGIC 0x31534452
typedef struct {u32 magic,size; s32 level,difficulty,orient; PlayerState ps; vec3 checkpoint; fx time; s32 best[5]; u32 nobj,nmodels,nglob; const char *strBase;} SaveHeader;
static bool available; static s32 best[5];
extern void *vm_globals(u32 *bytes); extern void vm_rebase(const char *oldBase); extern u32 keyItems[8];
void game_save_extra(s32 *orient); void game_load_extra(s32 orient);
void save_init(void){
  available=fatInitDefault();
  if(available){FILE *f=fopen(SAVE_PATH,"rb"); if(f){SaveHeader h; if(fread(&h,sizeof h,1,f)==1&&h.magic==SAVE_MAGIC)memcpy(best,h.best,sizeof best); fclose(f);}}
}
bool save_available(void){return available;}
bool save_exists(void){int a,b; return save_read_header(&a,&b);}
bool save_read_header(int *level,int *difficulty){
  if(!available)return false;
  FILE *f=fopen(SAVE_PATH,"rb"); if(!f)return false; SaveHeader h; bool ok=fread(&h,sizeof h,1,f)==1&&h.magic==SAVE_MAGIC&&h.level>=0; fclose(f);
  if(ok){*level=h.level;*difficulty=h.difficulty;}
  return ok;
}
void save_write(void){
  if(!available)return;
  FILE *f=fopen(SAVE_PATH,"wb"); if(!f)return;
  u32 gb; void *g=vm_globals(&gb); s32 orient; game_save_extra(&orient);
  SaveHeader h={SAVE_MAGIC,0,gameLevel,gameDifficulty,orient,ps,checkpoint,gameTime,{0},gd.nobj,level.h->nmodels,gb,gd.strData}; memcpy(h.best,best,sizeof best);
  fwrite(&h,sizeof h,1,f); fwrite(&player,sizeof player,1,f); fwrite(objects,sizeof(Object),gd.nobj,f); fwrite(modelState,sizeof(ModelState),level.h->nmodels,f); fwrite(g,1,gb,f); fwrite(keyItems,sizeof keyItems,1,f);
  fclose(f);
}
// The level has just been loaded and started; this puts the saved state on top of it.
bool save_restore(void){
  if(!available)return false;
  FILE *f=fopen(SAVE_PATH,"rb"); if(!f)return false;
  SaveHeader h; u32 gb; void *g=vm_globals(&gb);
  if(fread(&h,sizeof h,1,f)!=1||h.magic!=SAVE_MAGIC||h.level!=gameLevel||h.nobj!=gd.nobj||h.nmodels!=level.h->nmodels||h.nglob!=gb){fclose(f);return false;}
  ps=h.ps;checkpoint=h.checkpoint;gameTime=h.time;
  fread(&player,sizeof player,1,f);
  fread(objects,sizeof(Object),gd.nobj,f); for(u32 i=0;i<gd.nobj;i++)objects[i].d=&gd.objs[i];
  fread(modelState,sizeof(ModelState),level.h->nmodels,f); fread(g,1,gb,f); fread(keyItems,sizeof keyItems,1,f);
  fclose(f); vm_rebase(h.strBase); game_load_extra(h.orient); return true;
}
// The five best scores, kept across adventures.
void save_scores(s32 score){
  for(int i=0;i<5;i++)if(score>best[i]){for(int k=4;k>i;k--)best[k]=best[k-1]; best[i]=score; break;}
  if(!available)return;
  FILE *f=fopen(SAVE_PATH,"r+b");
  if(f){SaveHeader h; if(fread(&h,sizeof h,1,f)==1&&h.magic==SAVE_MAGIC){memcpy(h.best,best,sizeof best); fseek(f,0,SEEK_SET); fwrite(&h,sizeof h,1,f);} fclose(f); return;}
  f=fopen(SAVE_PATH,"wb"); if(f){SaveHeader h; memset(&h,0,sizeof h); h.magic=SAVE_MAGIC;h.level=-1;memcpy(h.best,best,sizeof best); fwrite(&h,sizeof h,1,f); fclose(f);}
}
const s32 *save_best(void){return best;}
