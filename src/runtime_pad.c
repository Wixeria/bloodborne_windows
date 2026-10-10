/* libScePad on SDL3 gamepads, keyboard and mouse. SDL events are pumped
 * by the window thread (gpu/shim/window.cpp); here state is only sampled.
 *
 * Default layout (editable in the Insert overlay; also works with a gamepad):
 *   WASD left stick, arrow keys right stick, Space Cross, LShift Circle,
 *   E Square, Q Triangle, 1 L1, 3 R1, R L2, F R2, Z L3, C R3,
 *   Enter Options, Tab left touchpad, Backspace right touchpad,
 *   IJKL d-pad (I up, K down, J left, L right), Ctrl walks, Esc goes back.
 * Mouse: movement right stick, left R1, right L2, middle R3,
 *   side 1 R2, side 2 L1. F8 toggles capture on the window thread. */
#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_clock.h"
#include "runtime_pad_input.h"
#include "gpu/bbgpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <SDL3/SDL.h>

#define ERR_INVALID_ARG ((int32_t)0x80920001)
#define ERR_INVALID_HANDLE ((int32_t)0x80920003)
#define ERR_ALREADY_OPENED ((int32_t)0x80920004)
#define ERR_NOT_INITIALIZED ((int32_t)0x80920005)
#define PAD_HANDLE 1

enum {
    BTN_L3=0x2, BTN_R3=0x4, BTN_OPTIONS=0x8, BTN_UP=0x10, BTN_RIGHT=0x20, BTN_DOWN=0x40, BTN_LEFT=0x80,
    BTN_L2=0x100, BTN_R2=0x200, BTN_L1=0x400, BTN_R1=0x800, BTN_TRIANGLE=0x1000, BTN_CIRCLE=0x2000,
    BTN_CROSS=0x4000, BTN_SQUARE=0x8000, BTN_TOUCHPAD=0x100000,
};
typedef struct { uint16_t x, y; uint8_t id, reserve[3]; } PadTouch;
typedef struct {
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2, analog_padding[2];
    float orientation[4], acceleration[3], angular_velocity[3];
    uint8_t touch_count, touch_reserve[3];
    uint32_t touch_held_time;
    PadTouch touches[2];
    uint8_t connected, pad0[3];
    uint64_t timestamp;
    uint8_t extension[16];
    uint8_t connected_count, reserve[2], unique_length, unique[12];
} PadData;
typedef struct {
    float pixel_density; uint16_t resolution_x, resolution_y;
    uint8_t dead_zone_left, dead_zone_right, connection_type, connected_count;
    uint8_t connected, pad[3];
    int32_t device_class;
    uint8_t reserve[8];
} ControllerInfo;
_Static_assert(sizeof(PadData)==120,"OrbisPadData layout");
_Static_assert(sizeof(PadTouch)==8,"OrbisPadTouch layout");
_Static_assert(__builtin_offsetof(PadData,touches)==60,"OrbisPadData touch offset");
_Static_assert(__builtin_offsetof(PadData,timestamp)==80,"OrbisPadData timestamp offset");
_Static_assert(sizeof(ControllerInfo)==28,"OrbisPadControllerInformation layout");

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int initialized, opened, sdl_ready;
static SDL_Gamepad *gamepad;
static size_t reads;
static uint8_t connected_count;

/* Protected by the same mutex as pad reads. Keep event processing independent of
 * guest polling: multiple scePadReadState calls must not consume or amplify motion.
 * Eight-ms velocity buckets also make high-DPI/event-rate mice usable without
 * tying camera speed to display resolution or the number of mouse events. */
static struct {
    int active, captured, enabled, invert_y, dpad_camera;
    bool keys[SDL_SCANCODE_COUNT];
    uint32_t mouse_buttons;
    float sensitivity, pending_x, pending_y, mouse_x, mouse_y;
    uint64_t bucket_start, published;
} host_input={.active=1,.enabled=1,.sensitivity=1.0f};

static int host_bindings[BB_BIND_COUNT]={
#define BB_BIND_DEFAULT(id, key, label, action, mask, primary, secondary) primary, secondary,
    BB_PAD_BIND_ACTIONS(BB_BIND_DEFAULT)
#undef BB_BIND_DEFAULT
};

static void clear_mouse_input(void) {
    host_input.mouse_buttons=0;
    host_input.pending_x=host_input.pending_y=0;
    host_input.mouse_x=host_input.mouse_y=0;
    host_input.bucket_start=host_input.published=0;
}

int runtime_pad_input_set_bindings(const int *bindings, unsigned count) {
    if (!bindings || count!=BB_BIND_COUNT) return 0;
    for (unsigned i=0;i<count;++i) if (!bb_pad_binding_valid(bindings[i])) return 0;
    pthread_mutex_lock(&lock);
    if (memcmp(host_bindings,bindings,sizeof(host_bindings))) {
        memcpy(host_bindings,bindings,sizeof(host_bindings));
        memset(host_input.keys,0,sizeof(host_input.keys));
        clear_mouse_input();
    }
    pthread_mutex_unlock(&lock);
    return 1;
}

void runtime_pad_input_configure(int active, int captured, int enabled,
                                 float sensitivity, int invert_y, int dpad_camera) {
    pthread_mutex_lock(&lock);
    active=!!active; captured=!!captured && active && enabled; enabled=!!enabled;
    if (host_input.active!=active || host_input.enabled!=enabled) {
        memset(host_input.keys,0,sizeof(host_input.keys));
        clear_mouse_input();
    }
    if (host_input.captured!=captured) clear_mouse_input();
    host_input.active=active; host_input.captured=captured; host_input.enabled=enabled;
    host_input.sensitivity=isfinite(sensitivity) ? SDL_clamp(sensitivity,0.1f,5.0f) : 1.0f;
    host_input.invert_y=!!invert_y;
    host_input.dpad_camera=!!dpad_camera;
    pthread_mutex_unlock(&lock);
}

void runtime_pad_input_event(const SDL_Event *event) {
    pthread_mutex_lock(&lock);
    if (host_input.active && host_input.enabled) {
        if (event->type==SDL_EVENT_KEY_DOWN || event->type==SDL_EVENT_KEY_UP) {
            SDL_Scancode key=event->key.scancode;
            if (key>SDL_SCANCODE_UNKNOWN && key<SDL_SCANCODE_COUNT && !event->key.repeat &&
                (event->type==SDL_EVENT_KEY_UP || !(event->key.mod&(SDL_KMOD_ALT|SDL_KMOD_GUI)) ||
                 key==SDL_SCANCODE_LALT || key==SDL_SCANCODE_RALT ||
                 key==SDL_SCANCODE_LGUI || key==SDL_SCANCODE_RGUI))
                host_input.keys[key]=event->type==SDL_EVENT_KEY_DOWN;
        } else if (host_input.captured && event->type==SDL_EVENT_MOUSE_MOTION &&
                   event->motion.which!=SDL_TOUCH_MOUSEID && event->motion.which!=SDL_PEN_MOUSEID) {
            if (isfinite(event->motion.xrel) && isfinite(event->motion.yrel)) {
                /* Bound a malformed event / long queued burst, with no motion backlog. */
                host_input.pending_x=SDL_clamp(host_input.pending_x+event->motion.xrel,-4096.0f,4096.0f);
                host_input.pending_y=SDL_clamp(host_input.pending_y+event->motion.yrel,-4096.0f,4096.0f);
            }
        } else if (host_input.captured && (event->type==SDL_EVENT_MOUSE_BUTTON_DOWN ||
                   event->type==SDL_EVENT_MOUSE_BUTTON_UP) &&
                   event->button.which!=SDL_TOUCH_MOUSEID && event->button.which!=SDL_PEN_MOUSEID) {
            uint32_t button=event->button.button>=SDL_BUTTON_LEFT && event->button.button<=SDL_BUTTON_X2
                ? SDL_BUTTON_MASK(event->button.button) : 0;
            if (event->type==SDL_EVENT_MOUSE_BUTTON_DOWN) host_input.mouse_buttons|=button;
            else host_input.mouse_buttons&=~button;
        }
    }
    pthread_mutex_unlock(&lock);
}

static void publish_mouse_at(uint64_t now) {
    if (!host_input.bucket_start) host_input.bucket_start=now;
    const uint64_t elapsed=now-host_input.bucket_start;
    if (elapsed<8000) return;
    /* The original game accepts an analog stick, including its dead zone and
     * maximum camera speed. Convert counts/second, then compensate its dead zone. */
    float x=host_input.pending_x*host_input.sensitivity*1000.0f/(float)elapsed;
    float y=host_input.pending_y*host_input.sensitivity*1000.0f/(float)elapsed;
    const float length=SDL_sqrtf(x*x+y*y);
    if (length>0) {
        const float gain=(0.12f+0.88f*SDL_min(length,1.0f))/length;
        x*=gain; y*=gain;
    }
    host_input.mouse_x=x; host_input.mouse_y=host_input.invert_y ? -y : y;
    host_input.pending_x=host_input.pending_y=0;
    host_input.bucket_start=host_input.published=now;
}

void runtime_pad_input_publish(void) {
    pthread_mutex_lock(&lock);
    if (host_input.captured) publish_mouse_at(runtime_monotonic_ns()/1000);
    pthread_mutex_unlock(&lock);
}

static uint8_t stick_axis(float v) {
    v=SDL_clamp(v,-1.0f,1.0f);
    return (uint8_t)SDL_roundf(128.0f+v*(v<0 ? 128.0f : 127.0f));
}

static void keyboard_stick(uint8_t *x, uint8_t *y, int left, int right,
                           int up, int down, int walk) {
    float dx=(float)(right-left), dy=(float)(down-up);
    if (!dx && !dy) return; /* connected controller remains usable */
    const float length=SDL_sqrtf(dx*dx+dy*dy);
    const float gain=(walk ? 0.5f : 1.0f)/length;
    *x=stick_axis(dx*gain); *y=stick_axis(dy*gain);
}

static void mouse_stick(PadData *d) {
    if (host_input.captured && d->timestamp-host_input.published<32000 &&
        (host_input.mouse_x || host_input.mouse_y)) {
        d->right_x=stick_axis(host_input.mouse_x);
        d->right_y=stick_axis(host_input.mouse_y);
    }
}

static void apply_dpad_camera(PadData *d) {
    if (!host_input.dpad_camera) return;
    const uint32_t directions=d->buttons&(BTN_UP|BTN_DOWN|BTN_LEFT|BTN_RIGHT);
    /* Do not fire the original item/menu actions while these directions drive
     * the camera. This includes physical pads and the configurable KBM binds. */
    d->buttons&=~(uint32_t)(BTN_UP|BTN_DOWN|BTN_LEFT|BTN_RIGHT);
    keyboard_stick(&d->right_x,&d->right_y,!!(directions&BTN_LEFT),!!(directions&BTN_RIGHT),
                   !!(directions&BTN_UP),!!(directions&BTN_DOWN),0);
}

static int unbound_camera_key(const bool *keys, SDL_Scancode key) {
    if (!keys[key]) return 0;
    /* An explicitly bound arrow must only perform its configured action. In
     * particular, a D-pad binding must respect the D-pad camera toggle. */
    for (int i=0;i<BB_BIND_COUNT;++i) if (host_bindings[i]==(int)key) return 0;
    return 1;
}

static uint64_t now_us(void) { return runtime_monotonic_ns()/1000; }
static uint8_t axis(int16_t v) { int x=(v+32768)>>8; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint8_t trigger(int16_t v) { int x=v>>7; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint16_t touch_axis(float v, int max) {
    return (uint16_t)(v<=0.0f ? 0 : v>=1.0f ? max : (int)(v*max+0.5f));
}
static void touch_click(PadData *d, int right) {
    d->buttons|=BTN_TOUCHPAD;
    d->touch_count=1;
    d->touches[0]=(PadTouch){.x=right ? 1440 : 480,.y=471,.id=0};
}

/* Opens the first gamepad SDL knows about; called under lock. */
static SDL_Gamepad *current_gamepad(void) {
    if (!sdl_ready) sdl_ready = SDL_WasInit(SDL_INIT_GAMEPAD) ? 1 : SDL_InitSubSystem(SDL_INIT_GAMEPAD) ? 1 : -1;
    if (sdl_ready<0) return NULL;
    if (gamepad && !SDL_GamepadConnected(gamepad)) { SDL_CloseGamepad(gamepad); gamepad=NULL; }
    if (!gamepad) {
        int count=0;
        SDL_JoystickID *ids=SDL_GetGamepads(&count);
        if (ids && count>0) {
            gamepad=SDL_OpenGamepad(ids[0]);
            if (gamepad) { ++connected_count; printf("Runtime: gamepad connected: %s\n",SDL_GetGamepadName(gamepad)); }
        }
        SDL_free(ids);
    }
    return gamepad;
}
static void sample_neutral(PadData *d) {
    memset(d,0,sizeof(*d));
    d->left_x=d->left_y=d->right_x=d->right_y=128;
    d->orientation[3]=1.0f;
    d->connected=1; d->connected_count=connected_count ? connected_count : 1;
    d->timestamp=now_us();
}
static void sample_host(PadData *d) {
    sample_neutral(d);
    SDL_Gamepad *g=current_gamepad();
    if (bbgpu_overlay_captures_input() || !host_input.active) return;
    const bool *k=host_input.enabled ? host_input.keys : NULL;
    if (g) {
        static const struct { SDL_GamepadButton sdl; uint32_t ps; } map[]={
            {SDL_GAMEPAD_BUTTON_SOUTH,BTN_CROSS}, {SDL_GAMEPAD_BUTTON_EAST,BTN_CIRCLE},
            {SDL_GAMEPAD_BUTTON_WEST,BTN_SQUARE}, {SDL_GAMEPAD_BUTTON_NORTH,BTN_TRIANGLE},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,BTN_L1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,BTN_R1},
            {SDL_GAMEPAD_BUTTON_LEFT_STICK,BTN_L3}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK,BTN_R3},
            {SDL_GAMEPAD_BUTTON_START,BTN_OPTIONS}, {SDL_GAMEPAD_BUTTON_BACK,BTN_TOUCHPAD},
            {SDL_GAMEPAD_BUTTON_TOUCHPAD,BTN_TOUCHPAD},
            {SDL_GAMEPAD_BUTTON_DPAD_UP,BTN_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN,BTN_DOWN},
            {SDL_GAMEPAD_BUTTON_DPAD_LEFT,BTN_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,BTN_RIGHT},
        };
        for (size_t i=0;i<sizeof(map)/sizeof(*map);++i) if (SDL_GetGamepadButton(g,map[i].sdl)) d->buttons|=map[i].ps;
        d->left_x=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTX)); d->left_y=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTY));
        d->right_x=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTX)); d->right_y=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTY));
        d->l2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)); d->r2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        if (d->l2>30) d->buttons|=BTN_L2;
        if (d->r2>30) d->buttons|=BTN_R2;
        if (SDL_GetNumGamepadTouchpads(g)>0) {
            const int fingers=SDL_GetNumGamepadTouchpadFingers(g,0);
            for (int finger=0;finger<fingers && d->touch_count<2;++finger) {
                bool down=false;
                float x=0, y=0;
                if (SDL_GetGamepadTouchpadFinger(g,0,finger,&down,&x,&y,NULL) && down) {
                    d->touches[d->touch_count++]=(PadTouch){.x=touch_axis(x,1919),
                        .y=touch_axis(y,942),.id=(uint8_t)finger};
                }
            }
        }
        // Back/Select on pads without a touch surface is a left-side click.
        if ((d->buttons & BTN_TOUCHPAD) && !d->touch_count) touch_click(d,0);
    }
    if (!k) { apply_dpad_camera(d); return; }
    uint32_t mapped=0;
    for (int action=0;action<BB_BIND_ACTION_COUNT;++action) {
        int down=0;
        for (int slot=0;slot<BB_BIND_SLOTS;++slot) {
            int code=host_bindings[action*BB_BIND_SLOTS+slot];
            down|=code>0 ? k[code] : code<0 && host_input.captured &&
                (host_input.mouse_buttons&SDL_BUTTON_MASK(-code));
        }
        if (!down) continue;
        mapped|=bb_pad_binding_actions[action].mask;
        if (action==BB_BIND_TOUCH_LEFT || action==BB_BIND_TOUCH_RIGHT)
            touch_click(d,action==BB_BIND_TOUCH_RIGHT);
    }
    d->buttons|=mapped;
    if (mapped&BTN_L2) d->l2=255;
    if (mapped&BTN_R2) d->r2=255;
    keyboard_stick(&d->left_x,&d->left_y,k[SDL_SCANCODE_A],k[SDL_SCANCODE_D],
                   k[SDL_SCANCODE_W],k[SDL_SCANCODE_S],k[SDL_SCANCODE_LCTRL]);
    keyboard_stick(&d->right_x,&d->right_y,unbound_camera_key(k,SDL_SCANCODE_LEFT),
                   unbound_camera_key(k,SDL_SCANCODE_RIGHT),unbound_camera_key(k,SDL_SCANCODE_UP),
                   unbound_camera_key(k,SDL_SCANCODE_DOWN),0);
    mouse_stick(d);
    apply_dpad_camera(d);
}

/* BB_PAD_FILE=<file>: scripted input for automated runs. The file holds whitespace-separated
 * tokens, re-read when it changes: button names (cross circle square triangle l1 r1 l2 r2 l3 r3
 * options touchpad touchpad_left touchpad_right up down left right) are held while listed;
 * touchpad defaults to a left-side click; lx= ly= rx= ry= (0..255) override
 * the sticks. An empty file releases everything. */
static struct { uint32_t buttons; int stick[4]; int touch_side; } injected={0,{-1,-1,-1,-1},-1};
static int replay_armed;      /* 1 while a BB_PAD_REPLAY recording plays, 2 once it ended */
static uint64_t replay_start; /* 0: (re)start at the next sample */
static void read_inject(void) {
    static const char *path; static int checked; static uint64_t last_check; static SDL_Time mtime;
    if (!checked) { path=getenv("BB_PAD_FILE"); checked=1; }
    if (!path || !*path) return;
    uint64_t now=now_us();
    if (now-last_check<20000) return;
    last_check=now;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path,&info)) return;
    if (info.modify_time==mtime) return;
    mtime=info.modify_time;
    FILE *f=fopen(path,"r");
    if (!f) return;
    static const struct { const char *name; uint32_t ps; } names[]={
        {"cross",BTN_CROSS}, {"circle",BTN_CIRCLE}, {"square",BTN_SQUARE}, {"triangle",BTN_TRIANGLE},
        {"l1",BTN_L1}, {"r1",BTN_R1}, {"l2",BTN_L2}, {"r2",BTN_R2}, {"l3",BTN_L3}, {"r3",BTN_R3},
        {"options",BTN_OPTIONS}, {"touchpad",BTN_TOUCHPAD},
        {"up",BTN_UP}, {"down",BTN_DOWN}, {"left",BTN_LEFT}, {"right",BTN_RIGHT},
    };
    static const char *sticks[]={"lx=","ly=","rx=","ry="};
    injected.buttons=0;
    injected.touch_side=-1;
    for (int i=0;i<4;++i) injected.stick[i]=-1;
    char token[64];
    while (fscanf(f,"%63s",token)==1) {
        if (!strcmp(token,"replay") && replay_armed!=1) { replay_armed=1; replay_start=0; } /* BB_PAD_REPLAY */
        if (!strcmp(token,"touchpad_left") || !strcmp(token,"touchpad_right")) {
            injected.buttons|=BTN_TOUCHPAD;
            injected.touch_side=!strcmp(token,"touchpad_right");
        }
        for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) if (!strcmp(token,names[i].name)) injected.buttons|=names[i].ps;
        for (int i=0;i<4;++i) if (!strncmp(token,sticks[i],3)) { int v=atoi(token+3); injected.stick[i]=v<0 ? 0 : v>255 ? 255 : v; }
    }
    fclose(f);
    printf("Runtime: pad file: buttons 0x%x sticks %d %d %d %d\n",injected.buttons,
           injected.stick[0],injected.stick[1],injected.stick[2],injected.stick[3]);
}
/* BB_PAD_RECORD=<file>: F9 starts and stops recording the pad state (gamepad or keyboard) with
 * the time since F9; BB_PAD_REPLAY=<file> plays such a recording back, started by the token
 * "replay" in BB_PAD_FILE (scripted tests repeat a route the player ran once). Lines: ms buttons
 * lx ly rx ry l2 r2, written when the state changes. */
typedef struct { uint32_t ms, buttons; uint8_t axes[4], l2, r2; } PadSample;
static FILE *record_file;
static uint64_t record_start;
static PadSample record_last;
static void record_sample(const PadData *d) {
    static const char *path; static int checked, f9_was_down;
    if (!checked) { path=getenv("BB_PAD_RECORD"); checked=1; }
    if (!path || !*path || !sdl_ready) return;
    const bool *k=SDL_GetKeyboardState(NULL);
    const int f9=k && k[SDL_SCANCODE_F9];
    if (f9 && !f9_was_down) {
        if (record_file) {
            fclose(record_file); record_file=NULL;
            printf("Runtime: pad recording stopped (%s)\n",path);
        } else if ((record_file=fopen(path,"w"))) {
            record_start=now_us();
            memset(&record_last,0xff,sizeof(record_last));
            printf("Runtime: pad recording started (%s, F9 stops)\n",path);
        }
    }
    f9_was_down=f9;
    if (!record_file) return;
    PadSample s={(uint32_t)((now_us()-record_start)/1000),d->buttons,
                 {d->left_x,d->left_y,d->right_x,d->right_y},d->l2,d->r2};
    if (s.buttons==record_last.buttons && !memcmp(s.axes,record_last.axes,4) &&
        s.l2==record_last.l2 && s.r2==record_last.r2) return;
    record_last=s;
    fprintf(record_file,"%u %u %u %u %u %u %u %u\n",s.ms,s.buttons,s.axes[0],s.axes[1],s.axes[2],
            s.axes[3],s.l2,s.r2);
    fflush(record_file);
}
static PadSample *replay; static size_t replay_count, replay_next;
static void replay_sample(PadData *d) {
    if (!replay_armed) return;
    if (!replay_start) {
        static int loaded;
        if (!loaded) {
            loaded=1;
            const char *path=getenv("BB_PAD_REPLAY");
            FILE *f=path ? fopen(path,"r") : NULL;
            PadSample s; unsigned v[8]; size_t cap=0;
            while (f && fscanf(f,"%u %u %u %u %u %u %u %u",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7])==8) {
                s=(PadSample){v[0],v[1],{(uint8_t)v[2],(uint8_t)v[3],(uint8_t)v[4],(uint8_t)v[5]},(uint8_t)v[6],(uint8_t)v[7]};
                if (replay_count==cap && !(replay=realloc(replay,(cap=cap ? cap*2 : 1024)*sizeof(*replay)))) break;
                replay[replay_count++]=s;
            }
            if (f) fclose(f);
            printf("Runtime: pad replay of %zu samples from %s\n",replay_count,path ? path : "(unset)");
        }
        replay_start=now_us();
        replay_next=0;
    }
    const uint32_t ms=(uint32_t)((now_us()-replay_start)/1000);
    while (replay_next<replay_count && replay[replay_next].ms<=ms) ++replay_next;
    if (!replay_next) return;
    if (replay_next==replay_count && ms>replay[replay_count-1].ms+500) {
        if (replay_armed==1) { puts("Runtime: pad replay finished"); replay_armed=2; }
        return;
    }
    const PadSample *s=&replay[replay_next-1];
    d->buttons=s->buttons;
    d->left_x=s->axes[0]; d->left_y=s->axes[1]; d->right_x=s->axes[2]; d->right_y=s->axes[3];
    d->l2=s->l2; d->r2=s->r2;
}
/* Touches as the DualShock 4 reports them: every finger that goes down gets a new id (1..127, kept
 * while it stays down) and the time since the first one went down. With id 0 and no hold time
 * the game ignored touchpad presses: no gesture menu from the touchpad, Back or Tab. */
static void touch_ids(PadData *d) {
    static uint8_t next_id=1, ids[2]; static int down[2]; static uint64_t since;
    for (int i=0;i<2;++i) {
        const int now=i<d->touch_count;
        if (now && !down[i]) { ids[i]=next_id; next_id=next_id==127 ? 1 : next_id+1; }
        down[i]=now;
        if (now) d->touches[i].id=ids[i];
    }
    if (!d->touch_count) since=0;
    else if (!since) since=d->timestamp;
    d->touch_held_time=d->touch_count ? (uint32_t)(d->timestamp-since) : 0;
}
static void sample(PadData *d) {
    /* Automated profiles must not mix keyboard/controller input into a route.
     * Opt in only with a script file; ordinary interactive input is unchanged. */
    const char *script_only=getenv("BB_PAD_SCRIPT_ONLY"), *script_path=getenv("BB_PAD_FILE");
    if (script_only && !strcmp(script_only,"1") && script_path && *script_path) sample_neutral(d);
    else sample_host(d);
    if (bbgpu_overlay_captures_input()) return;
    record_sample(d);
    read_inject();
    replay_sample(d);
    d->buttons|=injected.buttons;
    if (injected.touch_side>=0) touch_click(d,injected.touch_side);
    else if ((d->buttons & BTN_TOUCHPAD) && !d->touch_count) touch_click(d,0);
    if (injected.buttons & BTN_L2) d->l2=255;
    if (injected.buttons & BTN_R2) d->r2=255;
    uint8_t *axes[4]={&d->left_x,&d->left_y,&d->right_x,&d->right_y};
    for (int i=0;i<4;++i) if (injected.stick[i]>=0) *axes[i]=(uint8_t)injected.stick[i];
    touch_ids(d);
}

static ABI int32_t pad_init(void) { pthread_mutex_lock(&lock); initialized=1; pthread_mutex_unlock(&lock); return 0; }
static ABI int32_t pad_open(int32_t user, int32_t type, int32_t index, const void *param) {
    (void)param;
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (user!=1) return ERR_INVALID_ARG;
    if (type!=0 && type!=2) return ERR_INVALID_ARG; /* standard / special port */
    if (index) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    int already=opened; opened=1;
    pthread_mutex_unlock(&lock);
    if (already) return ERR_ALREADY_OPENED;
    puts("Runtime: pad opened for user 1 (SDL gamepad + keyboard/mouse; F8 releases cursor)");
    return PAD_HANDLE;
}
static ABI int32_t pad_close(int32_t handle) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    opened=0; return 0;
}
static ABI int32_t pad_read_state(int32_t handle, PadData *data) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data) return ERR_INVALID_ARG;
    bbgpu_reflex_input_sample((uintptr_t)__builtin_return_address(0));
    pthread_mutex_lock(&lock);
    sample(data); ++reads;
    pthread_mutex_unlock(&lock);
    return 0;
}
/* Buffered read: the port samples once per call, so one entry is returned. */
static ABI int32_t pad_read(int32_t handle, PadData *data, int32_t count) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data || count<1 || count>64) return ERR_INVALID_ARG;
    pad_read_state(handle,data);
    return 1;
}
static ABI int32_t pad_info(int32_t handle, ControllerInfo *info) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!info) return ERR_INVALID_ARG;
    memset(info,0,sizeof(*info));
    info->pixel_density=44.86f; info->resolution_x=1920; info->resolution_y=943;
    info->dead_zone_left=info->dead_zone_right=2;
    info->connection_type=0; info->connected=1; info->device_class=0;
    pthread_mutex_lock(&lock);
    current_gamepad();
    info->connected_count=connected_count ? connected_count : 1;
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t pad_vibration(int32_t handle, const uint8_t *param) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!param) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    SDL_Gamepad *g=current_gamepad();
    if (g) SDL_RumbleGamepad(g,(uint16_t)(param[0]*257),(uint16_t)(param[1]*257),1000);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t pad_ok_handle(int32_t handle) { return handle==PAD_HANDLE && opened ? 0 : ERR_INVALID_HANDLE; }
static ABI int32_t pad_ok_handle_flag(int32_t handle, uint8_t flag) { (void)flag; return pad_ok_handle(handle); }

static const RuntimeExport exports[]={
    {"scePadInit",pad_init}, {"scePadOpen",pad_open}, {"scePadClose",pad_close},
    {"scePadReadState",pad_read_state}, {"scePadRead",pad_read},
    {"scePadGetControllerInformation",pad_info}, {"scePadSetVibration",pad_vibration},
    {"scePadResetOrientation",pad_ok_handle},
    {"scePadSetAngularVelocityDeadbandState",pad_ok_handle_flag}, {"scePadSetTiltCorrectionState",pad_ok_handle_flag},
    {"scePadSetMotionSensorState",pad_ok_handle_flag},
};
uintptr_t runtime_pad_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_pad_report(void) { printf("Runtime: pad reads=%zu, gamepad=%s\n",reads,gamepad ? SDL_GetGamepadName(gamepad) : "none"); }
