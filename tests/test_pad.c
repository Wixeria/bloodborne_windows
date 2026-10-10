#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "../src/runtime_pad.c"

static int capture;
int bbgpu_overlay_captures_input(void) { return capture; }
void bbgpu_reflex_input_sample(uintptr_t caller) {
    (void)caller;
    /* Reflex may wait; the SDL event thread must remain able to update input. */
    assert(pthread_mutex_trylock(&lock)==0);
    pthread_mutex_unlock(&lock);
}
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    assert(runtime_sleep_until(runtime_monotonic_ns()+25000000)==0);
}

static void key_event(SDL_Scancode key, int down) {
    SDL_Event event={0};
    event.type=down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode=key;
    runtime_pad_input_event(&event);
}

static void pad_mouse_button(int button, int down) {
    SDL_Event event={0};
    event.type=down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button=(Uint8)button;
    runtime_pad_input_event(&event);
}

static void motion(float x, float y) {
    SDL_Event event={0};
    event.type=SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel=x; event.motion.yrel=y;
    runtime_pad_input_event(&event);
}

static void test_keyboard_mouse(void) {
    PadData d;
    runtime_pad_input_configure(1,1,1,1.0f,0,0);
    SDL_Event alt_tab={0}; alt_tab.type=SDL_EVENT_KEY_DOWN;
    alt_tab.key.scancode=SDL_SCANCODE_TAB; alt_tab.key.mod=SDL_KMOD_ALT;
    runtime_pad_input_event(&alt_tab);
    assert(!host_input.keys[SDL_SCANCODE_TAB]);
    key_event(SDL_SCANCODE_W,1);
    assert(pad_read_state(1,&d)==0 && d.left_y==0); /* even with a gamepad plugged in */
    key_event(SDL_SCANCODE_S,1);
    assert(pad_read_state(1,&d)==0 && d.left_y==128); /* opposing keys cancel exactly */
    key_event(SDL_SCANCODE_S,0); key_event(SDL_SCANCODE_D,1);
    assert(pad_read_state(1,&d)==0 && d.left_x==218 && d.left_y==37);
    key_event(SDL_SCANCODE_LCTRL,1);
    assert(pad_read_state(1,&d)==0 && d.left_x==173 && d.left_y==83);
    key_event(SDL_SCANCODE_SPACE,1); key_event(SDL_SCANCODE_ESCAPE,1);
    assert(pad_read_state(1,&d)==0 && (d.buttons&(BTN_CROSS|BTN_CIRCLE))==(BTN_CROSS|BTN_CIRCLE));

    pad_mouse_button(SDL_BUTTON_LEFT,1); pad_mouse_button(SDL_BUTTON_RIGHT,1);
    pad_mouse_button(SDL_BUTTON_MIDDLE,1); pad_mouse_button(SDL_BUTTON_X1,1); pad_mouse_button(SDL_BUTTON_X2,1);
    const uint32_t mouse_mask=BTN_R1|BTN_L2|BTN_R3|BTN_R2|BTN_L1;
    assert(pad_read_state(1,&d)==0 && (d.buttons&mouse_mask)==mouse_mask && d.l2==255 && d.r2==255);
    pad_mouse_button(SDL_BUTTON_LEFT,0); pad_mouse_button(SDL_BUTTON_RIGHT,0);
    pad_mouse_button(SDL_BUTTON_MIDDLE,0); pad_mouse_button(SDL_BUTTON_X1,0); pad_mouse_button(SDL_BUTTON_X2,0);
    assert(pad_read_state(1,&d)==0 && !(d.buttons&mouse_mask));

    /* Fixed event-time velocity buckets: pad polling must not consume motion. */
    host_input.bucket_start=100000;
    motion(4,0);
    publish_mouse_at(104000);
    assert(host_input.mouse_x==0 && host_input.pending_x==4);
    motion(4,0);
    publish_mouse_at(108000);
    assert(host_input.mouse_x==1 && host_input.pending_x==0);
    for (int i=0;i<100;++i) {
        sample_neutral(&d); d.timestamp=108000+(uint64_t)i*50;
        mouse_stick(&d);
        assert(d.right_x==255 && d.right_y==128);
    }
    sample_neutral(&d); d.timestamp=140000; mouse_stick(&d);
    assert(d.right_x==128 && d.right_y==128); /* window stall cannot leave the camera stuck */
    publish_mouse_at(116000);
    sample_neutral(&d); d.timestamp=116000; mouse_stick(&d);
    assert(d.right_x==128 && d.right_y==128); /* stationary mouse releases the stick */
    runtime_pad_input_configure(1,1,1,2.0f,1,0);
    motion(2,2); publish_mouse_at(124000);
    assert(host_input.mouse_x>0 && host_input.mouse_y<0);
    motion(4096,4096); publish_mouse_at(132000);
    assert(fabsf(host_input.mouse_x*host_input.mouse_x+host_input.mouse_y*host_input.mouse_y-1.0f)<0.0001f);

    /* A release/capture cycle clears old camera impulses and mouse buttons, but
     * leaves keyboard movement available. No gameplay mouse clicks while released. */
    pad_mouse_button(SDL_BUTTON_LEFT,1);
    runtime_pad_input_configure(1,0,1,1.0f,0,0);
    motion(100,100); pad_mouse_button(SDL_BUTTON_LEFT,1);
    assert(pad_read_state(1,&d)==0 && !(d.buttons&BTN_R1) && d.right_x==128 && d.left_y!=128);

    /* Overlay/text-entry/Alt-Tab blocking clears held keys too. A held/repeating
     * key from before blocking must be released and pressed again. */
    runtime_pad_input_configure(0,0,1,1.0f,0,0);
    key_event(SDL_SCANCODE_W,1); pad_mouse_button(SDL_BUTTON_LEFT,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==0 && d.left_y==128 && d.touch_count==0);
    runtime_pad_input_configure(1,1,1,1.0f,0,0);
    SDL_Event repeat={0}; repeat.type=SDL_EVENT_KEY_DOWN;
    repeat.key.scancode=SDL_SCANCODE_W; repeat.key.repeat=true;
    runtime_pad_input_event(&repeat);
    assert(pad_read_state(1,&d)==0 && d.left_y==128 && !(d.buttons&BTN_R1));
    key_event(SDL_SCANCODE_W,1);
    runtime_pad_input_configure(1,0,0,1.0f,0,0);
    assert(pad_read_state(1,&d)==0 && d.left_y==128 && !(d.buttons&BTN_CROSS));
    assert(d.buttons&BTN_TOUCHPAD); /* disabling KBM preserves the connected gamepad */
    runtime_pad_input_configure(1,0,1,1.0f,0,0);
}

static void test_rebinding(SDL_Joystick *joystick) {
    PadData d;
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    int bindings[BB_BIND_COUNT];
    memcpy(bindings,host_bindings,sizeof(bindings));
    runtime_pad_input_configure(1,1,1,1.0f,0,0);
    key_event(SDL_SCANCODE_SPACE,1);
    bindings[BB_BIND_CROSS*2]=SDL_SCANCODE_G;
    bindings[BB_BIND_CROSS*2+1]=-SDL_BUTTON_RIGHT;
    bindings[BB_BIND_L2*2+1]=0; /* free the default right mouse button */
    assert(runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
    assert(pad_read_state(1,&d)==0 && !(d.buttons&BTN_CROSS)); /* old held input released */
    key_event(SDL_SCANCODE_SPACE,1);
    assert(pad_read_state(1,&d)==0 && !(d.buttons&BTN_CROSS)); /* old hardcoded mapping removed */
    key_event(SDL_SCANCODE_G,1);
    assert(pad_read_state(1,&d)==0 && (d.buttons&BTN_CROSS));
    pad_mouse_button(SDL_BUTTON_RIGHT,1);
    key_event(SDL_SCANCODE_G,0); /* the second source must keep the action held */
    assert(pad_read_state(1,&d)==0 && (d.buttons&BTN_CROSS) && !(d.buttons&BTN_L2) && d.l2==0);
    pad_mouse_button(SDL_BUTTON_RIGHT,0);

    /* Exercise every configurable PS4 button from a non-default key. No old
     * mapping, hidden fallback or stale action may leak into the result. */
    memset(bindings,0,sizeof(bindings));
    const uint32_t expected[]={BTN_CROSS,BTN_TRIANGLE,BTN_SQUARE,BTN_CIRCLE,
        BTN_L1,BTN_L2,BTN_L3,BTN_R1,BTN_R2,BTN_R3,BTN_UP,BTN_DOWN,BTN_LEFT,BTN_RIGHT,
        BTN_OPTIONS,BTN_TOUCHPAD,BTN_TOUCHPAD};
    _Static_assert(sizeof(expected)/sizeof(*expected)==BB_BIND_ACTION_COUNT,"test every action");
    for (int a=0;a<BB_BIND_ACTION_COUNT;++a) {
        bindings[a*2]=SDL_SCANCODE_G;
        assert(runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
        key_event(SDL_SCANCODE_G,1);
        assert(pad_read_state(1,&d)==0 && d.buttons==expected[a]);
        if (a==BB_BIND_L2) assert(d.l2==255);
        if (a==BB_BIND_R2) assert(d.r2==255);
        if (a==BB_BIND_TOUCH_RIGHT) assert(d.touch_count==1 && d.touches[0].x==1440);
        key_event(SDL_SCANCODE_G,0);
        bindings[a*2]=0;
    }
    assert(runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
    key_event(SDL_SCANCODE_SPACE,1); pad_mouse_button(SDL_BUTTON_LEFT,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==0); /* all-unbound map is real */
    bindings[0]=SDL_SCANCODE_INSERT;
    assert(!runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
    assert(!runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT-1));
    assert(!runtime_pad_input_set_bindings(NULL,BB_BIND_COUNT));
    assert(pad_read_state(1,&d)==0 && d.buttons==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&d)==0 && d.buttons==BTN_CROSS); /* original physical pad mapping */
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    for (int a=0;a<BB_BIND_ACTION_COUNT;++a)
        for (int slot=0;slot<BB_BIND_SLOTS;++slot)
            bindings[a*2+slot]=bb_pad_binding_actions[a].defaults[slot];
    assert(runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
}

static void test_dpad_camera(SDL_Joystick *joystick) {
    PadData d;
    runtime_pad_input_configure(1,0,1,1.0f,0,0);
    key_event(SDL_SCANCODE_RIGHT,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==0 && d.right_x==255);
    key_event(SDL_SCANCODE_RIGHT,0);
    key_event(SDL_SCANCODE_I,1); key_event(SDL_SCANCODE_J,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==(BTN_UP|BTN_LEFT) && d.right_x==128 && d.right_y==128);
    runtime_pad_input_configure(1,0,1,1.0f,0,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==0 && d.right_x==37 && d.right_y==37);
    key_event(SDL_SCANCODE_J,0);
    key_event(SDL_SCANCODE_SPACE,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==BTN_CROSS && d.right_x==128 && d.right_y==0);
    key_event(SDL_SCANCODE_SPACE,0); key_event(SDL_SCANCODE_K,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==0 && d.right_y==128); /* up + down cancel */
    key_event(SDL_SCANCODE_K,0); key_event(SDL_SCANCODE_I,0);
    assert(pad_read_state(1,&d)==0 && d.right_y==128);

    int bindings[BB_BIND_COUNT];
    memcpy(bindings,host_bindings,sizeof(bindings));
    bindings[BB_BIND_DOWN*2]=SDL_SCANCODE_G;
    bindings[BB_BIND_UP*2+1]=-SDL_BUTTON_LEFT;
    bindings[BB_BIND_R1*2+1]=0;
    assert(runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
    runtime_pad_input_configure(1,1,1,1.0f,0,1);
    key_event(SDL_SCANCODE_K,1);
    assert(pad_read_state(1,&d)==0 && d.right_y==128 && d.buttons==0); /* honor the edited map */
    key_event(SDL_SCANCODE_K,0); key_event(SDL_SCANCODE_G,1);
    assert(pad_read_state(1,&d)==0 && d.right_y==255 && d.buttons==0);
    key_event(SDL_SCANCODE_G,0); pad_mouse_button(SDL_BUTTON_LEFT,1);
    assert(pad_read_state(1,&d)==0 && d.right_y==0 && d.buttons==0);
    pad_mouse_button(SDL_BUTTON_LEFT,0);
    assert(pad_read_state(1,&d)==0 && d.right_y==128);
    bindings[BB_BIND_DOWN*2]=SDL_SCANCODE_DOWN;
    assert(runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
    runtime_pad_input_configure(1,0,1,1.0f,0,0);
    key_event(SDL_SCANCODE_DOWN,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==BTN_DOWN && d.right_y==128);
    runtime_pad_input_configure(1,0,1,1.0f,0,1);
    assert(pad_read_state(1,&d)==0 && d.buttons==0 && d.right_y==255);
    runtime_pad_input_configure(1,0,1,1.0f,0,0);
    assert(pad_read_state(1,&d)==0 && d.buttons==BTN_DOWN && d.right_y==128);
    key_event(SDL_SCANCODE_DOWN,0);

    /* The physical D-pad also works with KBM disabled, without overriding the
     * actual right stick once released. Normal D-pad actions return on toggle-off. */
    runtime_pad_input_configure(1,0,0,1.0f,0,1);
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,16384));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&d)==0 && d.right_x==0 && d.buttons==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,false));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&d)==0 && d.right_x==192 && d.buttons==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_UP,true));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&d)==0 && d.right_y==0 && d.buttons==0);
    capture=1;
    assert(pad_read_state(1,&d)==0 && d.right_x==128 && d.right_y==128 && d.buttons==0);
    capture=0;
    runtime_pad_input_configure(0,0,0,1.0f,0,1);
    assert(pad_read_state(1,&d)==0 && d.right_y==128 && d.buttons==0);
    runtime_pad_input_configure(1,0,0,1.0f,0,0);
    assert(pad_read_state(1,&d)==0 && d.right_y==128 && d.buttons==BTN_UP);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_UP,false));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,0));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    for (int a=0;a<BB_BIND_ACTION_COUNT;++a)
        for (int slot=0;slot<BB_BIND_SLOTS;++slot)
            bindings[a*2+slot]=bb_pad_binding_actions[a].defaults[slot];
    assert(runtime_pad_input_set_bindings(bindings,BB_BIND_COUNT));
    runtime_pad_input_configure(1,0,1,1.0f,0,0);
}

int main(void) {
#ifdef _WIN32
    char directory[MAX_PATH],path[MAX_PATH];
    DWORD length=GetTempPathA(MAX_PATH,directory);
    assert(length>0 && length<MAX_PATH && GetTempFileNameA(directory,"bbp",0,path));
#else
    char path[]="/tmp/bbport-pad-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
#endif
    assert(SDL_setenv_unsafe("BB_PAD_FILE",path,1)==0);
#ifdef _WIN32
    /* The test EXE and SDL's prebuilt DLL can have separate CRT environments. */
    assert(_putenv_s("BB_PAD_FILE",path)==0);
#endif
    assert(SDL_setenv_unsafe("SDL_VIDEODRIVER","dummy",1)==0);
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471);
    const uint8_t first_touch_id=data.touches[0].id;
    assert(first_touch_id>0 && first_touch_id<=127);
    SDL_Delay(2);
    assert(pad_read_state(1,&data)==0 && data.touches[0].id==first_touch_id && data.touch_held_time>0);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);
    assert(data.touch_held_time==0);
    inject(path,"touchpad_left");
    assert(pad_read_state(1,&data)==0 && data.touches[0].id!=first_touch_id && data.touches[0].id>0);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.touch_count==0);

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="bbport test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    /* Virtual joystick axes are signed; SDL remaps trigger axes to 0..32767. */
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id>0);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id>0);
    assert(data.touches[0].id!=data.touches[1].id);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_CROSS));
#ifdef _WIN32
    assert(_putenv_s("BB_PAD_SCRIPT_ONLY","1")==0);
#else
    assert(setenv("BB_PAD_SCRIPT_ONLY","1",1)==0);
#endif
    inject(path,"r1 ly=0");
    assert(pad_read_state(1,&data)==0 && data.buttons==BTN_R1 && data.touch_count==0);
    assert(data.left_x==128 && data.left_y==0 && data.right_x==128 && data.right_y==128);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.left_y==128);
#ifdef _WIN32
    assert(_putenv_s("BB_PAD_SCRIPT_ONLY","0")==0);
#else
    assert(setenv("BB_PAD_SCRIPT_ONLY","0",1)==0);
#endif
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==480);
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-16384));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,0));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.l2>=63 && data.l2<=64 && data.r2>=127 && data.r2<=128);
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768));
    SDL_UpdateJoysticks(); SDL_UpdateGamepads();
    test_keyboard_mouse();
    test_rebinding(joystick);
    test_dpad_camera(joystick);
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    puts("PASS: pad ABI, touchpad, gamepad + KBM, movement, mouse actions/camera, focus/capture reset");
}
