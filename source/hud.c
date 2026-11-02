// Bottom screen: artwork from the original menu, with live status text on top.
#include "game.h"
#include <stdio.h>
#include <string.h>
static char msg[31]; static int msgTimer; static PrintConsole con;
void hud_init(void){
  videoSetModeSub(MODE_5_2D); vramSetBankC(VRAM_C_SUB_BG);
  int bg=bgInitSub(3,BgType_Bmp8,BgSize_B8_256x256,2,0);
  consoleInit(&con,0,BgType_Text4bpp,BgSize_T_256x256,4,1,false,true);
  bgSetPriority(con.bgId,0); bgSetPriority(bg,1);
  (void)bg;
}
void hud_load_art(void){
  FILE *f=fopen("nitro:/hud.bin","rb"); if(!f)return;
  static u16 pal[240]; fread(pal,2,240,f); dmaCopy(pal,BG_PALETTE_SUB,480);
  static u8 row[256]; u16 *dst=(u16*)BG_BMP_RAM_SUB(2);
  for(int y=0;y<192;y++){fread(row,1,256,f); DC_FlushRange(row,256); dmaCopyHalfWords(3,row,dst+y*128,256);}
  fclose(f);
  BG_PALETTE_SUB[255]=0xFFFF;
}
void hud_clear(void){consoleClear();}
// Subtitles wrap on word boundaries, in rows 7 to 20.
void hud_subtitle(const char *s){
  for(int r=7;r<21;r++)printf("\x1b[%d;0H%-31s",r,"");
  int row=7; const char *p=s;
  while(*p&&row<21){
    int n=strlen(p); if(n>30){n=30; while(n>0&&p[n]!=' ')n--; if(!n)n=30;}
    printf("\x1b[%d;1H%.*s",row++,n,p); p+=n; while(*p==' ')p++;
  }
}
void hud_hint(const char *s){printf("\x1b[22;1H%-30s",s);}
static char targetLine[32];
void hud_target(const char *name,int health,int maxHealth){
  if(!name){targetLine[0]=0;return;}
  static char bar[24]; int m=maxHealth>20?20:maxHealth,h=health>m?m:health; for(int i=0;i<m;i++)bar[i]=i<h?'#':'-'; bar[m]=0;
  snprintf(targetLine,sizeof targetLine,"%-9s %s",name,bar);
}
void hud_message(const char *s){snprintf(msg,sizeof msg,"%s",s);msgTimer=100;}
void hud_update(int fps){
  static char bar[24]; int h=ps.maxHealth>20?20:ps.maxHealth; for(int i=0;i<h;i++)bar[i]=i<ps.health?'#':'-'; bar[h]=0;
  printf("\x1b[1;1HLeven  %-20s\n Levens %ld      Punten %6ld\n Drank  %-2ld/%-2d   Geld   %5ld\n Spiegel %ld/5\n",bar,ps.lives,ps.score,ps.potions,potionTotal,ps.coins,ps.mirror);
  printf("\x1b[5;1H%-30s",msgTimer>0?msg:"");
  printf("\x1b[6;1H%-30s",targetLine);
  if(msgTimer>0)msgTimer-=60;
#if BENCH
  extern int statDrawn; printf("\x1b[21;1Hfps %2d in %4d ram %4d v %4d",fps,statDrawn,GFX_POLYGON_RAM_USAGE,GFX_VERTEX_RAM_USAGE);
#else
  (void)fps;
#endif
}
