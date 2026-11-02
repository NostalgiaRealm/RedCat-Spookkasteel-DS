// RedCat Spookkasteel, Nintendo DS version. Main loop, menu and level flow.
#include "game.h"
#include <stdio.h>
#include <filesystem.h>
volatile u32 vblanks;
static void on_vblank(void){vblanks++;}
void hud_load_art(void); void hud_clear(void); void audio_stop(void);
static const char *const LEVELS[5]={"Het Bos","Het Kasteel","Het Kerkhof","De Grotten","De Toren"};
static const u8 STANDARD_SKILL[5]={0,1,3,7,15};
static void fatal(const char *m){printf("\x1b[10;1H%s",m);while(1)swiWaitForVBlank();}
static bool load(int lvl){
  audio_stop();
  hud_clear(); printf("\x1b[11;9HLaden...\n\n         %s",LEVELS[lvl]);
  videoSetMode(MODE_0_3D);
  gameLevel=lvl; ps.potions=0;
  game_unload();
  if(!level_load(lvl))fatal("Level niet gevonden");
  if(!game_load(lvl))fatal("Gegevens niet geladen");
  render_level_ready(); player_spawn(); game_start(); hud_clear();
  return true;
}
#if BENCH
static u32 tSim,tDraw,tGx;
#define TIMING(x) x
#else
#define TIMING(x)
#endif
static void draw_frame(void){
  TIMING(u32 a=cpuGetTiming();)
  render_view(); render_sky(); render_cull(); game_draw_actors(); render_world();
  TIMING(u32 b=cpuGetTiming();)
  {int guard=0; while((GFX_STATUS&BIT(27))&&++guard<2000000);
   #if BENCH
   if(guard>=2000000){printf("\x1b[23;1HGX vast st=%08lx dma=%08lx",(unsigned long)GFX_STATUS,(unsigned long)DMA_CR(0));}
   #endif
  }
  TIMING(u32 c=cpuGetTiming(); tDraw=b-a; tGx=c-b;)
  render_finish();
}
static u32 lastSwap;
static int pace(void){
  swiWaitForVBlank();
  while(vblanks-lastSwap<2)swiWaitForVBlank();
  int dt=vblanks-lastSwap; if(dt>6)dt=6; lastSwap=vblanks; return dt;
}
#if BENCH==1
// Automatic tour past every enemy in all five levels. Results stay on the bottom screen.
static void bench(void){
  static int res[5][6];
  cpuStartTiming(0);
  ps.skill=3;
  for(int lvl=0;lvl<5;lvl++){
    load(lvl);
    game_skip_cutscene();
    const u32 *st=(const u32*)(level.base+level.h->oStops); u32 n=st[0]; if(n>18)n=18;
    u32 stop=0; int timer=0,frames=0,secs=0,maxP=0,late=0,totalFrames=0; u32 sec=vblanks,start=0; int dt=2; lastSwap=vblanks;
    while(stop<=n){
      if(timer--<=0){timer=45; const s32 *p=(const s32*)(st+1)+3*((stop*st[0]/(n+1))%st[0]); stop++; vec3 v={p[0],p[1]+FX(8),p[2]}; game_teleport(&v,0); camera_set_yaw(stop*5000);}
      ps.health=ps.maxHealth; levelDone=false;
      scanKeys(); u32 t0=cpuGetTiming();
      game_update(dt*4096/60,KEY_B|KEY_UP,0); game_skip_cutscene(); player_update(dt,4096,0,false,cam.yaw); camera_update(dt,20,0);
      tSim=cpuGetTiming()-t0;
      draw_frame(); int hw=GFX_POLYGON_RAM_USAGE; if(hw>maxP)maxP=hw;
      {int s=tSim/33514,d=tDraw/33514,g=tGx/33514; if(s>res[lvl][3])res[lvl][3]=s; if(d>res[lvl][4])res[lvl][4]=d; if(g>res[lvl][5])res[lvl][5]=g; printf("\x1b[20;1Hsim %2d draw %2d gx %2d  L%d stop %2lu/%lu",s,d,g,lvl+1,stop,n);}
      frames++;
      if(vblanks-sec>=60){int fps=frames*60/(vblanks-sec); hud_update(fps); if(!secs){start=vblanks;totalFrames=0;late=0;} secs++; frames=0; sec=vblanks;}
      dt=pace(); totalFrames++; if(secs&&dt>2)late++;
    }
    res[lvl][0]=(totalFrames*600)/(int)(vblanks-start); res[lvl][1]=late; res[lvl][2]=maxP;
  }
  hud_clear();
  printf("\x1b[1;0HLEVEL      FPS late poly sim drw\n");
  for(int i=0;i<5;i++)printf("%-10s %2d.%d %4d %4d %3d %3d\n",LEVELS[i],res[i][0]/10,res[i][0]%10,res[i][1],res[i][2],res[i][3],res[i][4]);
  printf(" KLAAR\n");
  while(1)swiWaitForVBlank();
}
#endif
#if BENCH==2
// Script stress test: fires every trigger of every level and runs the resulting scripts and motions.
#include <malloc.h>
static void stress(void){
  extern int vmUnknown; static int res[5][5];
  for(int lvl=0;lvl<5;lvl++){
    load(lvl); vmUnknown=0; int fired=0,maxB=0,opened=0;
    for(u32 i=0;i<gd.nobj;i++){Object *o=&objects[i];
      if(o->d->kind==K_TRIGGER){vmBudget=0; levelDone=false; game_trigger(o,true,false); fired++; if(vmBudget>maxB)maxB=vmBudget;}
      if(o->d->kind==K_DOOR){game_set_door(o,true);opened++;}
      if(o->d->kind==K_BUTTON){game_command(o,V_SWITCH);}
      if(i%40==0){for(int k=0;k<4;k++){vmBudget=0;motion_update(FX(.25));game_skip_cutscene();} printf("\x1b[20;1HL%d obj %lu/%lu  ",lvl+1,i,gd.nobj);}
    }
    for(int k=0;k<60;k++){vmBudget=0;motion_update(FX(.25));}
    levelDone=false;
    for(int f=0;f<30;f++){game_update(FX(1.0/30),0,0); levelDone=false; player_update(2,0,0,false,cam.yaw); camera_update(2,0,0); draw_frame(); swiWaitForVBlank();}
    struct mallinfo mi=mallinfo();
    res[lvl][0]=fired;res[lvl][1]=opened;res[lvl][2]=maxB;res[lvl][3]=vmUnknown;res[lvl][4]=(mi.uordblks+512)/1024;
  }
  hud_clear(); printf("\x1b[1;1HSTRESS trig door maxVM unk KiB\n");
  for(int i=0;i<5;i++)printf("%-11s %3d %3d %5d %3d %4d\n",LEVELS[i],res[i][0],res[i][1],res[i][2],res[i][3],res[i][4]);
  printf(" KLAAR\n"); while(1)swiWaitForVBlank();
}
#endif
#if BENCH==3
// Fixed camera poses from the main port (test/poses.h), one per A press, for side-by-side size checks.
#include "../test/poses.h"
static void poses(void){
  int cur=-1;
  for(u32 i=0;i<sizeof POSES/sizeof POSES[0];i++){const s32 *p=POSES[i];
    if(p[0]!=cur){cur=p[0];load(cur);dialogue_reset();fairy_clear();audio_stop();}
    cam.pos=(vec3){p[1],p[2],p[3]};cam.yaw=p[4];cam.pitch=p[5];
    printf("\x1b[20;1Hpose %lu level %d  ",i,cur);
    while(1){scanKeys(); if(keysDown()&KEY_A)break; draw_frame(); hud_update(0); swiWaitForVBlank();}
  }
  printf("\x1b[20;1HKLAAR       ");while(1)swiWaitForVBlank();
}
#endif
// Cinema bars during cutscenes, with the 3D layer shown only between them.
static void letterbox(bool on){
  if(on){*(vu16*)0x04000040=255;*(vu16*)0x04000044=(20<<8)|172;*(vu16*)0x04000048=1;*(vu16*)0x0400004A=0;REG_DISPCNT|=1<<13;}
  else REG_DISPCNT&=~(1<<13);
}
static const char *const DIFFICULTY[3]={"Makkelijk","Normaal","Moeilijk"};
// Returns -1 to continue the saved adventure, 0 for a new adventure, 1..5 for a single level.
static int menu(void){
  int sel=0; const bool hasSave=save_exists();
  load(0); dialogue_reset(); fairy_clear(); hud_clear(); letterbox(false);
  const int first=hasSave?-1:0; sel=first;
  int t=0; lastSwap=vblanks; int dt=2;
  while(1){
    scanKeys(); u32 d=keysDown();
    if(d&KEY_UP)sel=sel>first?sel-1:5; if(d&KEY_DOWN)sel=sel<5?sel+1:first;
    if(d&KEY_LEFT)gameDifficulty=(gameDifficulty+2)%3; if(d&KEY_RIGHT)gameDifficulty=(gameDifficulty+1)%3;
    if(d&(KEY_A|KEY_START))return sel;
    printf("\x1b[2;5HRedCat Spookkasteel\x1b[3;9HNintendo DS");
    if(hasSave)printf("\x1b[5;5H%c Verder spelen   ",sel==-1?'>':' ');
    printf("\x1b[6;5H%c Nieuw avontuur ",sel==0?'>':' ');
    for(int i=0;i<5;i++)printf("\x1b[%d;5H%c %d %-14s",7+i,sel==i+1?'>':' ',i+1,LEVELS[i]);
    printf("\x1b[13;5H< %-9s > moeilijkheid",DIFFICULTY[gameDifficulty]);
    {const s32 *b=save_best(); if(b[0]>0)printf("\x1b[15;5HBeste scores:\x1b[16;5H%ld  %ld  %ld",b[0],b[1],b[2]);}
    printf("\x1b[18;1HDpad lopen+draaien  A springen\x1b[19;1HB schieten en richten\x1b[20;1HL/R opzij  X/Y kijken");
    t+=dt; cam.yaw=(t*14)&0x7fff; cam.pitch=-600;
    vec3 c={level.h->spawn[0],level.h->spawn[1]+FX(70),level.h->spawn[2]}; cam.pos=c;
    draw_frame();
    dt=pace();
  }
}
// The level overview of the original: what was found, 15 points for each.
static void debriefing(int lvl){
  static const char *const ROWS[4]={"Toverdrank","Geldzakken","Vijanden","Geheimen"};
  s32 found[4],total[4],bonus=0; game_summary(found,total);
  const s32 before=ps.score;
  for(int i=0;i<4;i++)bonus+=found[i]*15;
  ps.score+=bonus; if(ps.score>999999)ps.score=999999;
  hud_clear(); printf("\x1b[1;8HLevel uitgespeeld!\x1b[2;10H%s",LEVELS[lvl]);
  for(int i=0;i<4;i++)printf("\x1b[%d;2H%-11s %3ld /%3ld  %4ld",5+i*2,ROWS[i],found[i],total[i],found[i]*15);
  printf("\x1b[14;2HPunten        %8ld\x1b[15;2HBonus         %8ld\x1b[16;2HNieuw totaal  %8ld",before,bonus,ps.score);
  for(int i=0;i<60;i++)swiWaitForVBlank();
  printf("\x1b[20;8HA: verder");
  while(1){swiWaitForVBlank();scanKeys(); if(keysDown()&(KEY_A|KEY_START))break;}
}
// Returns true to try again.
static bool retry_screen(void){
  hud_clear(); printf("\x1b[9;11HGAME OVER\x1b[12;5HA: opnieuw proberen\x1b[14;5HB: naar het menu");
  while(1){swiWaitForVBlank();scanKeys();u32 d=keysDown(); if(d&(KEY_A|KEY_START))return true; if(d&KEY_B)return false;}
}
static void hud_target_line(void){
  static const char *const NAMES[]={"Spin","Vleermuis","Spook","Zombie","Kikker","Skelet","Brutus","Brutus","Grizella","Wachter","Plant","Ridder","Waterspuwer","Max","Max"};
  const Object *t=game_target();
  if(!t){hud_target(NULL,0,0);return;}
  if(t->d->kind==K_ENEMY)hud_target(NAMES[t->d->p[0]],t->health,t->maxHealth);
  else if(t->d->kind==K_BUTTON)hud_target("Knop",0,0);
  else hud_target("Kist",0,0);
}
int main(void){
  defaultExceptionHandler();
  hud_init();
  render_init();
  if(!nitroFSInit(NULL))fatal("Bestanden niet gevonden");
  hud_load_art();
  irqSet(IRQ_VBLANK,on_vblank); irqEnable(IRQ_VBLANK);
  audio_init();
  dialogue_init();
  save_init();
#if BENCH==3
  poses();
#elif BENCH==2
  stress();
#elif BENCH==1
  bench();
#endif
  while(1){
#if BENCH==4
    int sel=SCENE_LEVEL+1;
#else
    int sel=menu(); audio_stop();
#endif
    ps=(PlayerState){10,10,3,0,0,0,0,0,0,0};
    // Without saved play history no level counts as played before, so the level scripts always run their scenes (GetGameType 0).
    gameReplay=false;
    int lvl=sel>0?sel-1:0,restoreLevel=-1,savedDiff=gameDifficulty;
    if(sel<0&&save_read_header(&restoreLevel,&savedDiff)){lvl=restoreLevel;gameDifficulty=savedDiff;}
    bool picked=sel>0;
    while(lvl<5){
      if(picked){ps.skill=STANDARD_SKILL[lvl]; if(lvl==3)ps.skill&=~4; picked=false;}  // the caves' own fairy still grants the charge shot
      ps.health=ps.maxHealth;
      PlayerState entry=ps;
      load(lvl);
      if(restoreLevel>=0){restoreLevel=-1; if(!save_restore())restoreLevel=-1; else entry=ps;}
      else save_write();
      lastSwap=vblanks; int dt=2; u32 sec=vblanks; int frames=0,fps=30; bool hint=false,bars=false;
      while(!levelDone){
        scanKeys(); u32 k=keysHeld(),kd=keysDown();
#if BENCH==4
        // Scene test: X skips the running cutscene, Y jumps straight to the named object.
        if(kd&KEY_X)game_skip_cutscene();
        if(kd&KEY_Y){Object *t=game_find(SCENE_OBJECT); if(t){vec3 v={t->x,t->y,t->z};
          if(t->d->brush>0){const BrushModel *bm=&level.models[t->d->brush]; const ModelState *ms=&modelState[t->d->brush]; v=(vec3){IFX((bm->min[0]+bm->max[0])/2)+ms->t[0],IFX(bm->max[1]+SCENE_ABOVE)+ms->t[1],IFX((bm->min[2]+bm->max[2])/2)+ms->t[2]};}
          game_teleport(&v,SCENE_FACING);}}
#endif
        if(kd&KEY_START){
          if(cutscene){game_skip_cutscene(); lastSwap=vblanks;}
          else { // pause
            printf("\x1b[8;11HGEPAUZEERD\x1b[10;5HSTART: verder spelen\x1b[11;5HSELECT: naar het menu");
            while(1){swiWaitForVBlank();scanKeys();u32 d=keysDown(); if(d&KEY_START)break; if(d&KEY_SELECT){lvl=99;break;}}
            hud_clear(); if(lvl==99)break; lastSwap=vblanks; continue;
          }
        }
        if(cutscene!=hint){hint=cutscene; hud_hint(hint?"Start: overslaan":"");}
        if(cutscene!=bars){bars=cutscene; letterbox(bars);}
        int fw=(k&KEY_UP?4096:0)-(k&KEY_DOWN?4096:0),st=(k&KEY_R?4096:0)-(k&KEY_L?4096:0);
        int turn=(k&KEY_LEFT?150:0)-(k&KEY_RIGHT?150:0),look=(k&KEY_X?60:0)-(k&KEY_Y?60:0);
        static int tx; touchPosition tp; if(k&KEY_TOUCH){touchRead(&tp); if(!(kd&KEY_TOUCH))turn+=(tx-tp.px)*20; tx=tp.px;}
        bool control=ps.health>0&&!game_control_locked();
        game_update(dt*4096/60,control?k:0,control?kd:0);
        if(levelDone)break;
        control=ps.health>0&&!game_control_locked();
        if(control){ if(player.grounded&&(kd&KEY_A))audio_sfx(S_JUMP); player_update(dt,fw,st,(k&KEY_A)!=0,cam.yaw);} else player_update(dt,0,0,false,cam.yaw);
        camera_update(dt,control?turn:0,control?look:0);
        draw_frame();
        frames++; if(vblanks-sec>=30){fps=frames*60/(vblanks-sec);frames=0;sec=vblanks;hud_target_line();hud_update(fps);}
        dt=pace();
      }
      letterbox(false);
      if(lvl==99)break;
      if(nextLevel==-2){
        save_scores(ps.score);
        if(!retry_screen()){lvl=99;break;}
        ps=entry;if(ps.lives<3)ps.lives=3;continue;
      }
      debriefing(lvl);
      lvl=nextLevel>lvl&&nextLevel<5?nextLevel:lvl+1;
    }
    letterbox(false);
    save_scores(ps.score);
    if(lvl==5){audio_stop();hud_clear();printf("\x1b[10;9HHET EINDE\x1b[12;5HGefeliciteerd, RedCat!\x1b[14;8HPunten %6ld",ps.score);for(int i=0;i<300;i++)swiWaitForVBlank();}
  }
}
