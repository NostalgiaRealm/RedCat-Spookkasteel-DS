// RedCat Spookkasteel, Nintendo DS version. Shared declarations.
#ifndef GAME_H
#define GAME_H
#include <nds.h>
typedef s32 fx;            // world units, 20.12 fixed point
#define FX(x) ((fx)((x)*4096))
#define IFX(x) ((fx)((x)<<12))
typedef struct {fx x,y,z;} vec3;

typedef struct {s32 n[3]; s32 d;} Plane;            // normal 2.30, distance 20.12
typedef struct {s16 c0,c1; u16 plane;} Node;
typedef struct {u16 contents,firstSide,numSides; s16 cluster; s16 min[3],max[3];} Leaf;
typedef struct {s16 min[3],max[3]; u32 list; u16 polys,words;} Cell;
typedef struct {s16 root; s16 min[3],max[3]; s16 origin[3]; u32 list; u16 polys,words,flags,pad;} BrushModel;  // flags: 1 visible liquid, 2 has liquid contents
typedef struct {u32 magic,ncells,nclusters,stride,nplanes,nnodes,nleaves,nsides,nmodels;
  u32 oLists,oCells,oClusterRow,oRows,oPlanes,oNodes,oLeaves,oSides,oModels;
  s32 spawn[3]; s32 yaw; u32 fog; u32 oStops;} LevelHeader;

typedef struct {
  LevelHeader *h; u8 *base; const u32 *lists; const Cell *cells; const u16 *clusterRow; const u8 *rows;
  const Plane *planes; const Node *nodes; const Leaf *leaves; const u16 *sides; const BrushModel *models;
  u32 sky[12]; u32 sprite[64];
} Level;
extern Level level;
bool level_load(int index);

// Moving brush model state, shared by collision and rendering.
typedef struct {bool active,solid,visible; fx t[3]; fx pivot[3]; s16 rot[9]; bool rotated;} ModelState;
extern ModelState *modelState;

// collide.c
typedef struct {s32 fraction; vec3 end; s32 normal[3]; bool startSolid; s16 model; s32 penDist; s32 penNormal[3];} Trace;   // fraction 20.12, normal 2.30; pen*: shortest way out of a solid start
void collide_trace(Trace *t,const vec3 *start,const vec3 *end,const fx *mins,const fx *maxs,bool withModels);
void collide_trace_skip(Trace *t,const vec3 *start,const vec3 *end,const fx *mins,const fx *maxs,int skipModel);
void collide_trace_only(Trace *t,const vec3 *start,const vec3 *end,const fx *mins,const fx *maxs,int model);
u32  collide_box_contents(int model,const vec3 *p,const fx *mins,const fx *maxs);
int  collide_leaf(const vec3 *p);

// render.c
typedef struct {vec3 pos; int yaw,pitch;} Camera;
extern Camera cam;
extern int statDrawn,statCells,statPolyBudget,statWorldNeed; extern s32 camRight[3],camUp[3];
void render_init(void);
void render_level_ready(void);
void render_view(void);
void render_sky(void);
void render_cull(void);
void render_world(void);
void render_finish(void);
void render_billboard(const vec3 *pos,int size,int sprite,u16 color,int alpha,int id,int angle);
void render_flat(const vec3 *pos,int half,int sprite,u16 color,int alpha,int id);
void render_strip(const vec3 *a,const vec3 *b,int width,int sprite,u16 color,int alpha,int id);
#define DRAW_DISTANCE 1150

// player.c
typedef struct {vec3 pos,vel; int yaw; bool grounded; fx launch[2]; fx env[3]; s16 contacts[8]; int ncontacts; bool prevJump,superUsed,jumped,jumpedSuper; fx jumpAge; fx landed; int inputFwd,inputSide;} Player;
extern Player player; extern bool cameraHidesPlayer;
extern const fx PLAYER_MINS[3],PLAYER_MAXS[3];
int atan2_bin(s32 y,s32 x);
void player_spawn(void);
void player_update(int dtVbl,int forward,int strafe,bool jump,int camYaw);
void camera_update(int dtVbl,int turn,int look);
void camera_set_yaw(int yaw);
void camera_turn_toward(int yaw,fx dt);
#endif
#ifndef GAME_H2
#define GAME_H2
#include "script_ids.h"
// ---- converted data (lN.gam) ----
typedef struct {u8 kind,cls; u16 flags; s32 x,y,z; s16 yaw,brush,model,motion; u16 name[3]; s16 p[8]; u16 scale;} __attribute__((packed)) ObjDef;
typedef struct {u8 kind,type,nparams; s8 selfIndex; s8 pad; u8 nslots; s16 native; u16 first,count; u8 ptypes[8];} __attribute__((packed)) FuncDef;
typedef struct {u8 tag,type; s16 pad; s32 val;} Expr;
typedef struct {u8 op,oper,when,pad; s32 target; Expr e0,e1,e2;} Instr;
typedef struct {u16 nameId,eventId,fn;} HandlerDef;
typedef struct {u16 first,count,nameId;} BindDef;
typedef struct {fx start,end,playEnd; s32 origin[3]; u32 oT,oR,oE; u16 nT,nR,nE,pad;} __attribute__((packed)) MotionDef;
typedef struct {u32 list; u16 words,polys; u8 bones,nclips; u16 clip; s16 radius,height,ymin,pad; s16 bmin[3],bmax[3]; u32 listFar; u16 wordsFar,polysFar; s16 spin[3],pad2;} __attribute__((packed)) ModelDef;
typedef struct {s16 mode,npoints,target; u16 first; fx duration; fx offset[3],pos[3],tpos[3]; s32 hasTpos;} CamDef;
typedef struct {char name[8]; u16 frames,rate; u32 data;} ClipDef;
typedef struct {s16 health,speed,attackRange,minDist,visual,sense,damage,score,viewAngle,rot4,remember16,drawPart,strikeStart,strikeEnd,avgShots16,waitShots16,waitSalvo16,moveChance,fallSpeed,rise16,look16,minH,maxH,machine16,ranged,pSprite,pSpeed,pDamage16,pLifeMin16,pLifeMax16,pGravity; s16 snd[6]; s16 pad;} EnemyStat;
typedef struct {s16 x,y,z; s16 links[4]; s16 pad;} WayPoint;
typedef struct {s32 nrPotions,nrMoney,nrEnemies,nrSecrets,nWaypoints,nStats; s32 shots[45];} LevelMeta;
enum {SND_IDLE1,SND_IDLE2,SND_ALERT,SND_ATTACK,SND_HURT,SND_DEATH};
enum {SP_FLEURI,SP_HALO,SP_RAYS,SP_STAR,SP_FLAME,SP_SMOKE_BLUE,SP_SMOKE_BROWN,SP_SMOKE_RED,SP_SMOKE_GREEN,SP_KABOOM,SP_EXPL,SP_SHADOW,SP_TARGET,SP_SCORE0,SP_SCORE1,SP_SCORE2,SP_SCORE3,SP_SCORE4,SP_BEAM,SP_ENERGY,SP_CORONA,SP_RING,SP_SPARK,SP_POWER,SP_SUPER,SP_BONE,SP_GOO,SP_JESTER,SP_MAGIC,SP_MAGMA,SP_MUSHROOM,SP_SKULL};
enum {K_LOGIC,K_DOOR,K_BUTTON,K_TRIGGER,K_CONTROLLER,K_SAVEPOINT,K_FAIRY,K_ACTOR,K_PICKUP,K_ENEMY,K_SPOUT,K_BEAM,K_CORONA};
enum {C_OTHER,C_CAMERA,C_TELEPORTER,C_SOUND,C_MUSIC,C_ENDPOINT,C_PLAYERSTART,C_WORLD,C_SAVEPOINT,C_FLASH,C_JESTERPT};
enum {A_IDLE,A_WALK,A_ATTACK,A_HIT,A_DEATH,A_START,A_RETRACT,A_DORMANT,A_TELEPORT};
enum {AMB_NONE,AMB_DORMANT,AMB_WAKING,AMB_AWAKE,AMB_DESCEND,AMB_RETRACT};
typedef struct {
  const ObjDef *d; fx x,y,z; fx vy; s16 yaw; s16 health,maxHealth;
  bool enabled,open,locked,switchedOn,visible,collected,moving,inside,hasMotion,motionStarted;
  u8 triggerPhase,buttonPhase; s16 triggerCount,switchCount; s8 pendingSwitch;
  fx closeAt; u16 cell;
  // motion player
  fx mTime,mFrom,mTo,mSpeed,mLoopFrom,mLoopTo; u8 mPlaying,mFinished,mLoop,mIncludeStart; u16 mGen;
  // enemy and animation
  u8 ai,clip; bool clipLoop; fx animTime,animRate,animUntil,attackTimer,memory; bool alerted,grounded,seen,pending,fired,secretFound,afterPending,bossHidden,bossInvuln,bossStarted,leftStart,relocate;
  fx pendAt,pendUntil,sightAt,thinkDt,soundUntil,stuck; u8 ambush,salvo,bossPhase; s8 bossPoint; fx lowerY,upperY,dropFrom,dropTo,bossT,bossDur,homeY,homeX,homeZ; s16 wpCur,wpPrev,wpTarget; u32 rng;
  // MoveTo/SetTo on a controller
  u8 ctKind; bool ctPending; fx ctTarget; u32 ctMask;
  fx pickupY,propDamage;
} Object;
typedef struct {s32 health,maxHealth,lives,score,potions,coins,mirror,skill,secrets,kills;} PlayerState;
extern int gameLevel,gameDifficulty; extern bool gameReplay; extern int potionTotal; extern int hintMotion;
typedef struct {
  u8 *base; u32 nobj,nstr,nbind,nhand,nfunc,nins,nglob,nmotion,nmodel,init; s32 gn[3],glo[3];
  const ObjDef *objs; const u32 *strOff; const char *strData; const BindDef *binds; const u16 *bindList; const HandlerDef *hands; const FuncDef *funcs;
  const Instr *ins; const u8 *globs; const MotionDef *motions; const u8 *motionData; const ModelDef *models; const ClipDef *clips; const u32 *modelLists; const u8 *frames; const u16 *grid;
  const CamDef *cams; const s32 *camPoints; const s32 *wind; const float *waypoints; const EnemyStat *stats; const WayPoint *wps; const LevelMeta *meta;
} GameData;
extern GameData gd; extern Object *objects; extern PlayerState ps; extern fx gameTime;
extern bool cutscene,enemiesFrozen,playerVisible,levelDone,cutsceneSkipping; extern int nextLevel; extern vec3 checkpoint;
void game_unload(void);
bool game_load(int index);
void game_start(void);
void game_update(fx dt,u32 keys,u32 down);
void game_draw_actors(void);
void game_event(Object *o,int ev,const char *label,fx time);
void game_command(Object *o,int verb);
void game_set_door(Object *o,bool open);
Object *game_find(const char *name);
void game_trigger(Object *o,bool explicit_,bool contact);
void game_kill(Object *o);
void game_damage(int amount);
void game_damage_fx(fx amount,bool continuous);
void game_teleport(const vec3 *p,int orientation);
void game_activate_camera(Object *o);
void game_cutscene_start(void);
void game_cutscene_stop(void);
void game_skip_cutscene(void);
bool game_control_locked(void);
void game_set_checkpoint(const vec3 *p,int orientation);
const CamDef *game_camera(fx *elapsed);
void game_sound(Object *o);
void game_music(Object *o,int mode);
void game_destroy(Object *o);
void game_secret(Object *o);
void game_reset_object(Object *o);
const Object *game_target(void);
void game_shot(int sprite,const vec3 *pos,const s32 *dir,fx speed,fx gravity,fx life,fx damage,bool homing);
bool game_los(const vec3 *a,const vec3 *b);
void game_summary(s32 *found,s32 *total);
// enemies.c
const EnemyStat *enemy_stat(const Object *o);
void enemy_init(Object *o);
void enemy_place(Object *o);
void enemy_think(Object *o,fx dt);
void enemy_dying(Object *o);
void enemy_hit(Object *o,fx amount);
void enemy_defeat(Object *o);
void enemy_aim(const Object *o,vec3 *out);
bool enemy_visible(const Object *o);
fx   clip_seconds(const Object *o,int clip);
// effects.c
void fx_reset(void);
void fx_update(fx dt);
int  fx_draw(void);
void fx_spawn(int sprite,const vec3 *p,fx vx,fx vy,fx vz,fx life,int s0,int s1,int a0,int a1,u16 color,fx gravity,int spin);
void fx_explosion(const vec3 *p,int size);
void fx_smoke(const vec3 *p);
void fx_impact(const vec3 *p,int kind);
void fx_reward(const vec3 *p,int score,bool pickup);
void fx_flash(Object *o);
void fx_teleporter(Object *o);
// save.c
void save_init(void);
bool save_available(void);
bool save_exists(void);
void save_write(void);
bool save_read_header(int *level,int *difficulty);
bool save_restore(void);
void save_scores(s32 score);
const s32 *save_best(void);
// motion
void motion_start(Object *o,int to256);
void motion_update(fx dt);
// script.c
void vm_init(void);
void vm_run_init(void);
void vm_dispatch_name(u16 nameId,int ev,const char *label,fx time);
extern int vmBudget;
// actors
void actors_begin(void);
int  model_clip(int model,const char *name);
int  actor_draw(int model,bool far,const vec3 *pos,const s16 *modelRot,fx age,int yaw,int pitchX,int rollZ,u16 scale,int clip,fx time,bool loop,u16 color);
u16  light_at(const vec3 *p);
const char *str_at(u32 id);
// audio.c
void audio_init(void);
void audio_sfx(int id);
void audio_music(int track);
void audio_level(int index);
int  audio_level_sfx(int id);
int  audio_level_at(int id,const vec3 *p,int volume);
void audio_sfx_at(int id,const vec3 *p,int volume);
void audio_loops(void);
void audio_loops_reset(void);
void audio_mute_all(bool on);
void audio_charge(bool on);
int  audio_voice(int id);     // returns the length in milliseconds
void audio_voice_stop(void);
void audio_fairy_idle(bool on);
enum {S_JUMP,S_SHOT,S_SHOT2,S_CHARGE,S_IMPACT,S_IMPACT2,S_IMPACT3,S_HURT1,S_HURT2,S_HURT3,S_DEATH,S_LAND,S_STEP1,S_STEP2,S_COIN1,S_COIN2,S_COIN3,S_HEALTH1,S_HEALTH2,S_HEALTH3,S_POTION,S_MIRROR,S_LIFE,S_HART,S_SECRET,
  S_BTN_PUSH,S_BTN_HANDLE,S_BTN_TRIGGER,S_DOOR_NORMAL,S_DOOR_KEY,S_DOOR_SECRET,S_ERROR,S_EXPLOSION,S_LOCK1,S_UNLOCK1,S_LOCK2,S_UNLOCK2,S_LOCK3,S_UNLOCK3,S_FAIRY_APPEAR,S_FAIRY_IDLE,S_FAIRY_LEAVE,S_COUNT};
// dialogue.c
void dialogue_init(void);
void dialogue_reset(void);
void dialogue_say(const char *id);
void dialogue_update(fx dt);
bool dialogue_busy(void);
// hud
void hud_init(void);
void hud_update(int fps);
void hud_message(const char *s);
void hud_subtitle(const char *s);
void hud_hint(const char *s);
void hud_target(const char *name,int health,int maxHealth);
// fairy.c
void fairy_reset(void);
void fairy_enable(Object *o,bool on);
void fairy_update(fx dt);
int  fairy_draw(void);
void fairy_clear(void);
#endif
