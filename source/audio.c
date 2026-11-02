#include "game.h"
#include <stdio.h>
#include <stdlib.h>
static u8 *sfx; static u32 sfxCount; static const u32 *sfxTable; static const u8 *sfxData;
static u8 *musicBuf; static int musicChannel=-1,musicTrack=-1;
static u8 *levelSfx; static u32 levelCount; static const u32 *levelTable; static const u8 *levelData;
static u8 *voiceBuf; static int voiceChannel=-1,fairyChannel=-1;
#define VOICE_MAX (96*1024)
#define MUSIC_MAX (296*1024)
void audio_init(void){
  soundEnable();
  FILE *f=fopen("nitro:/sfx.bin","rb"); if(!f)return;
  fseek(f,0,SEEK_END); size_t n=ftell(f); fseek(f,0,SEEK_SET); sfx=malloc(n); if(sfx){fread(sfx,1,n,f); DC_FlushRange(sfx,n); sfxCount=*(u32*)sfx; sfxTable=(const u32*)(sfx+4); sfxData=(const u8*)(sfxTable+sfxCount*2);} fclose(f);
  musicBuf=malloc(MUSIC_MAX); voiceBuf=malloc(VOICE_MAX);
}
void audio_sfx(int id){
  if(!sfx||id<0||(u32)id>=sfxCount||cutsceneSkipping)return;
  soundPlaySample(sfxData+sfxTable[id*2],SoundFormat_ADPCM,sfxTable[id*2+1],8000,110,64,false,0);
}
void audio_charge(bool on);
void audio_loops_reset(void);
void audio_stop(void){audio_charge(false);audio_loops_reset();if(musicChannel>=0)soundKill(musicChannel);musicChannel=-1;musicTrack=-1;}
void audio_music(int track){
  if(track==musicTrack||!musicBuf)return;
  if(musicChannel>=0){soundKill(musicChannel);musicChannel=-1;}
  char name[24]; sprintf(name,"nitro:/m%d.adp",track);
  FILE *f=fopen(name,"rb"); if(!f)return;
  size_t n=fread(musicBuf,1,MUSIC_MAX,f); fclose(f); n&=~3; DC_FlushRange(musicBuf,n); musicTrack=track;
  musicChannel=soundPlaySample(musicBuf,SoundFormat_ADPCM,n,10000,70,64,true,1);
}
void audio_level(int index){
  audio_loops_reset(); free(levelSfx); levelSfx=NULL; levelCount=0;
  char name[24]; sprintf(name,"nitro:/l%d.snd",index);
  FILE *f=fopen(name,"rb"); if(!f)return;
  fseek(f,0,SEEK_END); size_t n=ftell(f); fseek(f,0,SEEK_SET); levelSfx=malloc(n);
  if(levelSfx){fread(levelSfx,1,n,f); DC_FlushRange(levelSfx,n); levelCount=*(u32*)levelSfx; levelTable=(const u32*)(levelSfx+4); levelData=(const u8*)(levelTable+levelCount*2);}
  fclose(f);
}
int audio_level_sfx(int id){
  if(!levelSfx||id<0||(u32)id>=levelCount||!levelTable[id*2+1]||cutsceneSkipping)return 0;
  soundPlaySample(levelData+levelTable[id*2],SoundFormat_ADPCM,levelTable[id*2+1],8000,100,64,false,0);
  return levelTable[id*2+1]/4;   // two samples per byte at 8000 Hz
}
// Volume and pan by distance from the camera. Full up to 16 metres, silent at 80.
static bool spatial(const vec3 *p,int volume,int *vol,int *pan){
  int dx=(p->x-cam.pos.x)>>12,dy=(p->y-cam.pos.y)>>12,dz=(p->z-cam.pos.z)>>12;
  if(abs(dx)>2560||abs(dy)>2560||abs(dz)>2560)return false;
  int d=sqrt32(dx*dx+dy*dy+dz*dz); if(d>=2560)return false;
  int v=d<=512?volume:volume*(2560-d)/2048; if(v<2)return false; *vol=v;
  s32 side=(dx*cosLerp(cam.yaw)-dz*sinLerp(cam.yaw))>>12; int q=d?64+side*48/d:64; *pan=q<0?0:q>127?127:q;
  return true;
}
int audio_level_at(int id,const vec3 *p,int volume){
  if(!levelSfx||id<0||(u32)id>=levelCount||!levelTable[id*2+1]||cutsceneSkipping)return 0;
  int vol,pan; if(!spatial(p,volume,&vol,&pan))return 0;
  soundPlaySample(levelData+levelTable[id*2],SoundFormat_ADPCM,levelTable[id*2+1],8000,vol,pan,false,0);
  return levelTable[id*2+1]/4;
}
void audio_sfx_at(int id,const vec3 *p,int volume){
  if(!sfx||id<0||(u32)id>=sfxCount||!sfxTable[id*2+1]||cutsceneSkipping)return;
  int vol,pan; if(!spatial(p,volume,&vol,&pan))return;
  soundPlaySample(sfxData+sfxTable[id*2],SoundFormat_ADPCM,sfxTable[id*2+1],8000,vol,pan,false,0);
}
void audio_charge(bool on){
  static int ch=-1; if(ch>=0){soundKill(ch);ch=-1;}
  if(on&&sfx&&sfxTable[S_CHARGE*2+1])ch=soundPlaySample(sfxData+sfxTable[S_CHARGE*2],SoundFormat_ADPCM,sfxTable[S_CHARGE*2+1],8000,110,64,false,0);
}
// Ambience: the two nearest looping EffectSounds play, the rest wait. Sounds with a replay delay repeat on their own timer.
#define LOOPS 2
static struct {s16 obj; int channel;} loops[LOOPS]; static u32 loopTick;
void audio_loops_reset(void){for(int i=0;i<LOOPS;i++){ if(loops[i].channel>=0&&loops[i].obj>=0)soundKill(loops[i].channel); loops[i].obj=-1;loops[i].channel=-1;}}
void audio_loops(void){
  if(++loopTick&7)return;
  int best[LOOPS]={-1,-1}; s32 bd[LOOPS]={0x7fffffff,0x7fffffff};
  if(!cutsceneSkipping)for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i]; if(o->d->cls!=C_SOUND||!o->enabled||!o->d->p[1]||o->d->p[0]<0)continue;
    int dx=(o->x-cam.pos.x)>>12,dy=(o->y-cam.pos.y)>>12,dz=(o->z-cam.pos.z)>>12; if(abs(dx)>2000||abs(dy)>2000||abs(dz)>2000)continue; s32 d=(dx>>2)*(dx>>2)+(dy>>2)*(dy>>2)+(dz>>2)*(dz>>2);
    if(o->d->p[3]>0){ if(gameTime>=o->closeAt){vec3 c={o->x,o->y,o->z}; audio_level_at(o->d->p[0],&c,110); o->closeAt=gameTime+(o->d->p[2]<<8)+(((o->d->p[3]-o->d->p[2])<<8)*(int)(loopTick&15)>>4);} continue; }
    if(d<bd[0]){bd[1]=bd[0];best[1]=best[0];bd[0]=d;best[0]=i;} else if(d<bd[1]){bd[1]=d;best[1]=i;}
  }
  for(int k=0;k<LOOPS;k++){bool keep=false; for(int j=0;j<LOOPS;j++)if(loops[k].obj==best[j]&&best[j]>=0)keep=true; if(!keep&&loops[k].channel>=0){soundKill(loops[k].channel);loops[k].channel=-1;loops[k].obj=-1;}}
  for(int j=0;j<LOOPS;j++){int i=best[j]; if(i<0)continue; const Object *o=&objects[i]; vec3 c={o->x,o->y,o->z}; int vol=64,pan=64; bool audible=o->d->p[4]?spatial(&c,70,&vol,&pan):true; if(!o->d->p[4])vol=50;
    int slot=-1; for(int k=0;k<LOOPS;k++)if(loops[k].obj==i)slot=k;
    if(slot<0){ if(!audible||!levelSfx||(u32)o->d->p[0]>=levelCount||!levelTable[o->d->p[0]*2+1])continue; for(int k=0;k<LOOPS;k++)if(loops[k].obj<0){slot=k;break;} if(slot<0)continue;
      loops[slot].obj=i;loops[slot].channel=soundPlaySample(levelData+levelTable[o->d->p[0]*2],SoundFormat_ADPCM,levelTable[o->d->p[0]*2+1],8000,vol,pan,true,1); }
    else if(loops[slot].channel>=0){soundSetVolume(loops[slot].channel,audible?vol:0);soundSetPan(loops[slot].channel,pan);}
  }
}
void audio_mute_all(bool on){(void)on;}
void audio_voice_stop(void){
  if(voiceChannel>=0){soundKill(voiceChannel);voiceChannel=-1;}
  if(musicChannel>=0)soundSetVolume(musicChannel,70);
}
int audio_voice(int id){
  audio_voice_stop(); if(!voiceBuf)return 0;
  char name[24]; sprintf(name,"nitro:/v%d.adp",id);
  FILE *f=fopen(name,"rb"); if(!f)return 0;
  size_t n=fread(voiceBuf,1,VOICE_MAX,f); fclose(f); n&=~3; if(n<8)return 0;
  DC_FlushRange(voiceBuf,n);
  if(musicChannel>=0)soundSetVolume(musicChannel,30);
  voiceChannel=soundPlaySample(voiceBuf,SoundFormat_ADPCM,n,11025,127,64,false,0);
  return (int)(((n-4)*2)*1000/11025);
}
void audio_fairy_idle(bool on){
  if(fairyChannel>=0){soundKill(fairyChannel);fairyChannel=-1;}
  if(on&&sfx&&S_FAIRY_IDLE<(int)sfxCount)fairyChannel=soundPlaySample(sfxData+sfxTable[S_FAIRY_IDLE*2],SoundFormat_ADPCM,sfxTable[S_FAIRY_IDLE*2+1],8000,50,64,true,1);
}
