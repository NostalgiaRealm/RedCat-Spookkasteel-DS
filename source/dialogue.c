// Cutscene dialogue: subtitles and voice lines in order. A scene's end waits until the queue has finished.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {char key[32]; u32 text; s16 voice; u16 duration;} __attribute__((packed)) Line;
static u8 *data; static u32 count; static const Line *lines; static const char *texts;
#define QUEUE 8
static const Line *queue[QUEUE]; static int qHead,qCount; static fx left; static bool playing;
void dialogue_init(void){
  FILE *f=fopen("nitro:/dlg.bin","rb"); if(!f)return;
  fseek(f,0,SEEK_END); size_t n=ftell(f); fseek(f,0,SEEK_SET); data=malloc(n); if(data){fread(data,1,n,f); count=*(u32*)data; lines=(const Line*)(data+4); texts=(const char*)(lines+count);} fclose(f);
}
void dialogue_reset(void){qHead=qCount=0;playing=false;left=0;audio_voice_stop();hud_subtitle("");}
static const Line *find(const char *id){
  char key[32]; int i=0; for(;id[i]&&i<31;i++)key[i]=(id[i]>='A'&&id[i]<='Z')?id[i]+32:id[i]; key[i]=0;
  char *dot=strstr(key,".wav"); if(dot)*dot=0;
  for(u32 k=0;k<count;k++) if(!strcmp(lines[k].key,key))return &lines[k];
  return NULL;
}
static void start(const Line *l){
  hud_subtitle(texts+l->text); playing=true;
  int ms=l->voice>=0?audio_voice(l->voice):0;
  fx dur=ms>0?(fx)(ms*4096/1000):(fx)(l->duration<<8);
  if(ms<=0&&dur<FX(3))dur=FX(3);
  left=dur;
}
void dialogue_say(const char *id){
  const Line *l=find(id); if(!l)return;
  if(cutsceneSkipping||(l->voice<0&&!texts[l->text]))return;
  if(qCount<QUEUE){queue[(qHead+qCount)%QUEUE]=l;qCount++;}
  if(!playing){const Line *n=queue[qHead]; start(n);}
}
void dialogue_update(fx dt){
  if(!playing)return;
  left-=dt;
  if(left>0)return;
  qHead=(qHead+1)%QUEUE; qCount--; playing=false;
  if(qCount>0)start(queue[qHead]); else {audio_voice_stop();hud_subtitle("");}
}
bool dialogue_busy(void){return playing;}
