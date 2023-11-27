/*B-em v2.2 by Tom Walker
  Main loop + start/finish code*/

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_native_dialog.h>
#include <allegro5/allegro_primitives.h>

#include "6502.h"
#include "adc.h"
#include "model.h"
#include "cmos.h"
#include "config.h"
#include "csw.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "fdi.h"
#include "hfe.h"
#include "gui-allegro.h"
#include "i8271.h"
#include "ide.h"
#include "joystick.h"
#include "keyboard.h"
#include "keydef-allegro.h"
#include "led.h"
#include "main.h"
#include "6809tube.h"
#include "mem.h"
#include "mouse.h"
#include "midi.h"
#include "music4000.h"
#include "music5000.h"
#include "mmccard.h"
#include "paula.h"
#include "pal.h"
#include "savestate.h"
#include "scsi.h"
#include "sdf.h"
#include "serial.h"
#include "sid_b-em.h"
#include "sn76489.h"
#include "sound.h"
#include "sysacia.h"
#include "tape.h"
#include "tapecat-allegro.h"
#include "tapenoise.h"
#include "tube.h"
#include "via.h"
#include "sysvia.h"
#include "uef.h"
#include "uservia.h"
#include "vdfs.h"
#include "video.h"
#include "video_render.h"
#include "wd1770.h"

#include "tube.h"
#include "NS32016/32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "x86_tube.h"
#include "z80.h"
#include "sprow.h"

#undef printf

bool quitting = false;
bool keydefining = false;
bool autopause = false;
bool portable_mode = false;
int autoboot=0;
int joybutton[2];
float joyaxes[4];
int emuspeed = 4;

static ALLEGRO_TIMER *timer;
ALLEGRO_EVENT_QUEUE *queue;
static ALLEGRO_EVENT_SOURCE evsrc;

ALLEGRO_DISPLAY *tmp_display;

typedef enum {
    FSPEED_NONE,
    FSPEED_SELECTED,
    FSPEED_RUNNING
} fspeed_type_t;

static double time_limit;
static int fcount = 0;
static fspeed_type_t fullspeed = FSPEED_NONE;
static bool bempause  = false;

const emu_speed_t emu_speeds[NUM_EMU_SPEEDS] = {
    {  "10%", 1.0 / (50.0 * 0.10), 1 },
    {  "25%", 1.0 / (50.0 * 0.25), 1 },
    {  "50%", 1.0 / (50.0 * 0.50), 1 },
    {  "75%", 1.0 / (50.0 * 0.75), 1 },
    { "100%", 1.0 / 50.0,          2 },
    { "150%", 1.0 / (50.0 * 1.50), 2 },
    { "200%", 1.0 / (50.0 * 2.00), 2 },
    { "300%", 1.0 / (50.0 * 3.00), 3 },
    { "400%", 1.0 / (50.0 * 4.00), 4 },
    { "500%", 1.0 / (50.0 * 5.00), 5 }
};

void main_reset()
{
    m6502_reset();
    crtc_reset();
    video_reset();
    sysvia_reset();
    uservia_reset();
    serial_reset();
    wd1770_reset();
    i8271_reset();
    scsi_reset();
    vdfs_reset();
    sid_reset();
    music4000_reset();
    music5000_reset();
    paula_reset();
    sn_init();
    if (curtube != -1) tubes[curtube].reset();
    else               tube_exec = NULL;
    tube_reset();
}

static const char helptext[] =
    VERSION_STR " command line options:\n\n"
    "-mx             - start as model x (see readme.txt for models)\n"
    "-tx             - start with tube x (see readme.txt for tubes)\n"
    "-disc disc.ssd  - load disc.ssd into drives :0/:2\n"
    "-disc1 disc.ssd - load disc.ssd into drives :1/:3\n"
    "-autoboot       - boot disc in drive :0\n"
    "-tape tape.uef  - load tape.uef\n"
    "-fasttape       - set tape speed to fast\n"
    "-Fx             - set maximum video frames skipped\n"
    "-s              - scanlines display mode\n"
    "-i              - interlace display mode\n"
    "-spx            - Emulation speed x from 0 to 9 (default 4)\n"
    "-debug          - start debugger\n"
    "-debugtube      - start debugging tube processor\n"
    "-exec file      - debugger to execute file\n"
    "-paste string   - paste string in as if typed\n"
    "-vroot host-dir - set the VDFS root\n"
    "-vdir guest-dir - set the initial (boot) dir in VDFS\n"
    "-fullscreen     - start fullscreen\n\n";

void main_init(int argc, char *argv[])
{
    bool start_fullscreen = false;
    int tapenext = 0, discnext = 0, execnext = 0, vdfsnext = 0, pastenext = 0;
    ALLEGRO_DISPLAY *display;
    ALLEGRO_PATH *path;
    const char *ext, *exec_fn = NULL;
    const char *vroot = NULL, *vdir = NULL;

    if (!al_init()) {
        fputs("Failed to initialise Allegro!\n", stderr);
        exit(1);
    }

    if (check_portable_txt()) {
        portable_mode = true;
    }
    else {
        //We really need to do it here, before config loading
        for (int c = 1; c < argc; c++) {
            if (!strcasecmp(argv[c], "-portable")) { portable_mode = true; break; }
        }
    }

    al_init_native_dialog_addon();
    al_set_new_window_title(VERSION_STR);
    al_init_primitives_addon();
    if (!al_install_keyboard()) {
        log_fatal("main: unable to install keyboard");
        exit(1);
    }
    key_init();
    config_load();
    log_open();
    log_info("main: starting %s", VERSION_STR);

    model_loadcfg();

    for (int c = 1; c < argc; c++) {
        if (!strcasecmp(argv[c], "--help") || !strcmp(argv[c], "-?") || !strcasecmp(argv[c], "-h")) {
            fwrite(helptext, sizeof helptext - 1, 1, stdout);
            exit(1);
        }
        else if (!strncasecmp(argv[c], "-sp", 3)) {
            sscanf(&argv[c][3], "%i", &emuspeed);
            if (!(emuspeed < NUM_EMU_SPEEDS))
                emuspeed = 4;
        }
        else if (!strcasecmp(argv[c], "-tape"))
            tapenext = 2;
        else if (!strcasecmp(argv[c], "-disc") || !strcasecmp(argv[c], "-disk"))
            discnext = 1;
        else if (!strcasecmp(argv[c], "-disc1"))
            discnext = 2;
        else if (argv[c][0] == '-' && (argv[c][1] == 'm' || argv[c][1] == 'M'))
            sscanf(&argv[c][2], "%i", &curmodel);
        else if (argv[c][0] == '-' && (argv[c][1] == 't' || argv[c][1] == 'T'))
            sscanf(&argv[c][2], "%i", &curtube);
        else if (!strcasecmp(argv[c], "-fasttape"))
            fasttape = true;
        else if (!strcasecmp(argv[c], "-autoboot"))
            autoboot = 150;
        else if (!strcasecmp(argv[c], "-fullscreen"))
            start_fullscreen = true;
        else if (argv[c][0] == '-' && (argv[c][1] == 'f' || argv[c][1] == 'F')) {
            sscanf(&argv[c][2], "%i", &vid_fskipmax);
            if (vid_fskipmax < 1) vid_fskipmax = 1;
            if (vid_fskipmax > 9) vid_fskipmax = 9;
        }
        else if (argv[c][0] == '-' && (argv[c][1] == 's' || argv[c][1] == 'S'))
            vid_dtype_user = VDT_SCANLINES;
        else if (!strcasecmp(argv[c], "-debug"))
            debug_core = 1;
        else if (!strcasecmp(argv[c], "-debugtube"))
            debug_tube = 1;
        else if (argv[c][0] == '-' && (argv[c][1] == 'i' || argv[c][1] == 'I'))
            vid_dtype_user = VDT_INTERLACE;
        else if (!strcasecmp(argv[c], "-exec"))
            execnext = 1;
        else if (!strcasecmp(argv[c], "-vroot"))
            vdfsnext = 1;
        else if (!strcasecmp(argv[c], "-vdir"))
            vdfsnext = 2;
        else if (!strcasecmp(argv[c], "-paste"))
            pastenext = 1;
        else if (tapenext) {
            if (tape_fn)
                al_destroy_path(tape_fn);
            tape_fn = al_create_path(argv[c]);
        }
        else if (discnext) {
            if (discfns[discnext - 1])
                al_destroy_path(discfns[discnext - 1]);
            discfns[discnext - 1] = al_create_path(argv[c]);
            discnext = 0;
        }
        else if (execnext) {
            exec_fn = argv[c];
            execnext = 0;
        }
        else if (vdfsnext) {
            if (vdfsnext == 2)
                vdir = argv[c];
            else
                vroot = argv[c];
            vdfsnext = 0;
        }
        else if (pastenext) {
            argv[c] = strreplace(argv[c], "\\r\\n", "\n");
            argv[c] = strreplace(argv[c], "\\n", "\n");
            argv[c] = strreplace(argv[c], "\\r", "\n");
            os_paste_start(strdup(argv[c]));
        }
        else {
            path = al_create_path(argv[c]);
            ext = al_get_path_extension(path);
            if (ext && !strcasecmp(ext, ".snp"))
                savestate_load(argv[c]);
            else if (ext && (!strcasecmp(ext, ".uef") || !strcasecmp(ext, ".csw"))) {
                if (tape_fn)
                    al_destroy_path(tape_fn);
                tape_fn = path;
                tapenext = 0;
            }
            else {
                if (discfns[0])
                    al_destroy_path(discfns[0]);
                discfns[0] = path;
                discnext = 0;
                autoboot = 150;
            }
        }
        if (tapenext) tapenext--;
    }

    display = video_init();
    if (start_fullscreen) {
        fullscreen = 1;
        video_enterfullscreen();
    }

    mode7_makechars();
    al_init_image_addon();
    led_init();

    mem_init();

    if (!(queue = al_create_event_queue())) {
        log_fatal("main: unable to create event queue");
        exit(1);
    }
    al_register_event_source(queue, al_get_display_event_source(display));

    if (!al_install_audio()) {
        log_fatal("main: unable to initialise audio");
        exit(1);
    }
    if (!al_reserve_samples(3)) {
        log_fatal("main: unable to reserve audio samples");
        exit(1);
    }
    if (!al_init_acodec_addon()) {
        log_fatal("main: unable to initialise audio codecs");
        exit(1);
    }

    sound_init();
    sid_init();
    sid_settype(sidmethod, cursid);
    music5000_init(queue);
    paula_init();
    ddnoise_init();
    tapenoise_init(queue);

    adc_init();
    pal_init();
    disc_init();
    fdi_init();
    hfe_init();

    scsi_init();
    ide_init();
    vdfs_init(vroot, vdir);

    model_init();

    midi_init();
    main_reset();

    joystick_init(queue);

    tmp_display = display;

    if (!start_fullscreen) {
        gui_allegro_init(queue, display);
    }

    time_limit = 2.0 / 50.0;
    if (!(timer = al_create_timer(1.0 / 50.0))) {
        log_fatal("main: unable to create timer");
        exit(1);
    }
    al_register_event_source(queue, al_get_timer_event_source(timer));
    al_init_user_event_source(&evsrc);
    al_register_event_source(queue, &evsrc);

    al_register_event_source(queue, al_get_keyboard_event_source());

    oldmodel = curmodel;

    al_install_mouse();
    al_register_event_source(queue, al_get_mouse_event_source());

    if (mmb_fn)
        mmb_load(mmb_fn);
    else
        disc_load(0, discfns[0]);
    disc_load(1, discfns[1]);
    tape_load(tape_fn);
    if (mmccard_fn)
        mmccard_load(mmccard_fn);
    if (defaultwriteprot)
        writeprot[0] = writeprot[1] = 1;
    if (discfns[0])
        gui_set_disc_wprot(0, writeprot[0]);
    if (discfns[1])
        gui_set_disc_wprot(1, writeprot[1]);
    main_setspeed(emuspeed);
    debug_start(exec_fn);
}

void main_restart()
{
    main_pause("restarting");
    cmos_save(&models[oldmodel]);

    model_init();
    main_reset();
    main_resume();
}

int resetting = 0;
int framesrun = 0;
bool fastforward = false;
void main_cleardrawit()
{
    fcount = 0;
}

void main_start_fullspeed(void)
{
    fastforward = true;
    if (fullspeed != FSPEED_RUNNING) {
        ALLEGRO_EVENT event;

        log_debug("main: starting full-speed");
        al_stop_timer(timer);
        fullspeed = FSPEED_RUNNING;
        event.type = ALLEGRO_EVENT_TIMER;
        al_emit_user_event(&evsrc, &event, NULL);
    }
}

void main_stop_fullspeed(bool hostshift)
{
    fastforward = false;
    if (emuspeed != EMU_SPEED_FULL) {
        if (!hostshift) {
            log_debug("main: stopping fullspeed (PgUp)");
            if (fullspeed == FSPEED_RUNNING && emuspeed != EMU_SPEED_PAUSED)
                al_start_timer(timer);
            fullspeed = FSPEED_NONE;
        }
        else
            fullspeed = FSPEED_SELECTED;
    }
}

void main_key_break(void)
{
    m6502_reset();
    video_reset();
    i8271_reset();
    wd1770_reset();
    sid_reset();
    music5000_reset();
    cmos_reset();
    paula_reset();

    if (curtube != -1)
        tubes[curtube].reset();
    tube_reset();
}

void main_key_fullspeed(void)
{
    if (fullspeed != FSPEED_RUNNING)
        main_start_fullspeed();
}

void main_key_pause(void)
{
    if (bempause) {
        if (emuspeed != EMU_SPEED_PAUSED) {
            bempause = false;
            if (emuspeed != EMU_SPEED_FULL)
                al_start_timer(timer);
        }
    } else {
        al_stop_timer(timer);
        bempause = true;
    }
}

int save_slot = 0;
const char* quick_save_hud;
int quick_save_hud_alpha = -1;
void main_quick_save(void)
{
    char* prefix = "QuickSave";
    if (discfns[0] != NULL) {
        prefix = al_get_path_basename(discfns[0], ALLEGRO_NATIVE_PATH_SEP);
    }
    else if (discfns[1] != NULL) {
        prefix = al_get_path_basename(discfns[1], ALLEGRO_NATIVE_PATH_SEP);
    }
    char suffix[3];
    suffix[0] = '_';
    suffix[1] = save_slot + '0';
    suffix[2] = '\0';
    const char* filename = malloc(strlen(prefix) + 3);
    strcpy(filename, prefix);
    strcat(filename, suffix);

    ALLEGRO_PATH* path = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
    al_append_path_component(path, "states");
    const char* cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    CreateDirectory(cpath, NULL);
    al_set_path_filename(path, filename);
    cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    savestate_save(cpath);
    
    //hud
    if (quick_save_hud != NULL) free(quick_save_hud);
    prefix = "Saved State: ";
    quick_save_hud = malloc(strlen(prefix) + strlen(filename) + 5);
    strcpy(quick_save_hud, prefix);
    strcat(quick_save_hud, filename);
    strcat(quick_save_hud, ".snp");
    quick_save_hud_alpha = 255;

    //free(filename);
}
void main_quick_load(void)
{
    char* prefix = "QuickSave";
    if (discfns[0] != NULL) {
        prefix = al_get_path_basename(discfns[0], ALLEGRO_NATIVE_PATH_SEP);
    }
    else if (discfns[1] != NULL) {
        prefix = al_get_path_basename(discfns[1], ALLEGRO_NATIVE_PATH_SEP);
    }
    char* ext = ".snp";
    char suffix[3];
    suffix[0] = '_';
    suffix[1] = save_slot + '0';
    suffix[2] = '\0';
    const char* filename = malloc(strlen(prefix) + 6);
    strcpy(filename, prefix);
    strcat(filename, suffix);
    strcat(filename, ext);

    if (quick_save_hud != NULL) free(quick_save_hud);

    ALLEGRO_PATH* path = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
    al_append_path_component(path, "states");
    al_set_path_filename(path, filename);
    const char* cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    DWORD dwAttrib = GetFileAttributes(cpath);
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        savestate_load(cpath);

        //hud
        prefix = "Loaded State: ";
    }
    else {
        prefix = "State not found: ";
    }
    quick_save_hud = malloc(strlen(prefix) + strlen(filename) + 1);
    strcpy(quick_save_hud, prefix);
    strcat(quick_save_hud, filename);
    quick_save_hud_alpha = 255;

    //free(filename);
}
void main_quick_slot_prev(void)
{
    save_slot--;
    if (save_slot < 0) save_slot = 0;

    if (quick_save_hud != NULL) free(quick_save_hud);
    char* prefix = "Set QuickSave slot: ";
    char suffix[2];
    suffix[0] = save_slot + '0'; suffix[1] = '\0';
    quick_save_hud = malloc(strlen(prefix) + 2);
    strcpy(quick_save_hud, prefix);
    strcat(quick_save_hud, suffix);
    quick_save_hud_alpha = 255;
}
void main_quick_slot_next(void)
{
    save_slot++;
    if (save_slot > 9) save_slot = 9;

    if (quick_save_hud != NULL) free(quick_save_hud);
    char* prefix = "Set QuickSave slot: ";
    char suffix[2];
    suffix[0] = save_slot + '0'; suffix[1] = '\0';
    quick_save_hud = malloc(strlen(prefix) + 2);
    strcpy(quick_save_hud, prefix);
    strcat(quick_save_hud, suffix);
    quick_save_hud_alpha = 255;
}

double prev_time = 0;
int execs = 0;
double spd = 0;

static void main_timer(ALLEGRO_EVENT *event)
{
    double now = al_get_time();
    double delay = now - event->any.timestamp;

    if (delay < time_limit) {
        if (autoboot)
            autoboot--;
        framesrun++;

        if (x65c02)
            m65c02_exec();
        else
            m6502_exec();
        execs++;

        if (ddnoise_ticks > 0 && --ddnoise_ticks == 0)
            ddnoise_headdown();

        if (tapeledcount) {
            if (--tapeledcount == 0 && !motor) {
                log_debug("main: delayed cassette motor LED off");
                led_update(LED_CASSETTE_MOTOR, 0, 0);
            }
        }
        if (led_ticks > 0 && --led_ticks == 0)
            led_timer_fired();

        if (savestate_wantload)
            savestate_doload();
        if (savestate_wantsave)
            savestate_dosave();
        if (fullspeed == FSPEED_RUNNING)
            al_emit_user_event(&evsrc, event, NULL);

        if (now - prev_time > 0.1) {

            double speed = execs * 40000 / (now - prev_time);

            if (spd < 0.01)
                spd = 100.0 * speed / 2000000;
            else
                spd = spd * 0.75 + 0.25 * (100.0 * speed / 2000000);


            char buf[120];
            snprintf(buf, 120, "%s %.3fMHz %.1f%%", VERSION_STR, speed / 1000000, spd);
            al_set_window_title(tmp_display, buf);

            execs = 0;
            prev_time = now;
        }
    }
}

static double last_switch_in = 0.0;

void main_run()
{
    ALLEGRO_EVENT event;

    log_debug("main: about to start timer");
    al_start_timer(timer);

    log_debug("main: entering main loop");
    while (!quitting) {
        al_wait_for_event(queue, &event);
        switch(event.type) {
            case ALLEGRO_EVENT_KEY_DOWN:
                if (!keydefining)
                    key_down_event(&event);
                break;
            case ALLEGRO_EVENT_KEY_CHAR:
                if (!keydefining)
                    key_char_event(&event);
                break;
            case ALLEGRO_EVENT_KEY_UP:
                if (!keydefining)
                    key_up_event(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_AXES:
                mouse_axes(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                log_debug("main: mouse button down");
                mouse_btn_down(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
                log_debug("main: mouse button up");
                mouse_btn_up(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_AXIS:
                joystick_axis(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN:
                joystick_button_down(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_UP:
                joystick_button_up(&event);
                break;
            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                log_debug("main: event display close - quitting");
                quitting = true;
                break;
            case ALLEGRO_EVENT_TIMER:
                main_timer(&event);
                break;
            case ALLEGRO_EVENT_MENU_CLICK:
                main_pause("menu active");
                gui_allegro_event(&event);
                main_resume();
                break;
            case ALLEGRO_EVENT_AUDIO_STREAM_FRAGMENT:
                music5000_streamfrag();
                break;
            case ALLEGRO_EVENT_DISPLAY_RESIZE:
                video_update_window_size(&event);
                break;
            case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
                /* bodge for when OUT events immediately follow an IN event */
                if ((event.any.timestamp - last_switch_in) > 0.01) {
                    key_lost_focus();
                    if (autopause && !debug_core && !debug_tube)
                        main_pause("auto-paused");
                }
                break;
            case ALLEGRO_EVENT_DISPLAY_SWITCH_IN:
                last_switch_in = event.any.timestamp;
                if (autopause)
                    main_resume();
        }
    }
    log_debug("main: end loop");
}

void main_close()
{
    gui_tapecat_close();
    gui_keydefine_close();

    debug_kill();

    config_save();
    cmos_save(&models[curmodel]);

    midi_close();
    mem_close();
    uef_close();
    csw_close();
    tube_6502_close();
    arm_close();
    x86_close();
    z80_close();
    w65816_close();
    n32016_close();
    mc6809nc_close();
    sprow_close();
    disc_close(0);
    disc_close(1);
    scsi_close();
    ide_close();
    vdfs_close();
    music5000_close();
    ddnoise_close();
    tapenoise_close();

    video_close();
    log_close();
}

void main_setspeed(int speed)
{
    log_debug("main: setspeed %d", speed);
    if (speed == EMU_SPEED_FULL)
        main_start_fullspeed();
    else {
        al_stop_timer(timer);
        fullspeed = FSPEED_NONE;
        if (speed != EMU_SPEED_PAUSED) {
            if (speed >= NUM_EMU_SPEEDS) {
                log_warn("main: speed #%d out of range, defaulting to 100%%", speed);
                speed = 4;
            }
            al_set_timer_speed(timer, emu_speeds[speed].timer_interval);
            time_limit = emu_speeds[speed].timer_interval * 2.0;
            vid_fskipmax = emu_speeds[speed].fskipmax;
            log_debug("main: new speed#%d, timer interval=%g, vid_fskipmax=%d", speed, emu_speeds[speed].timer_interval, vid_fskipmax);
            al_start_timer(timer);
        }
    }
    emuspeed = speed;
}

void main_pause(const char *why)
{
    char buf[120];
    snprintf(buf, sizeof(buf), "%s (%s)", VERSION_STR, why);
    al_set_window_title(tmp_display, buf);
    al_stop_timer(timer);
}

void main_resume(void)
{
    if (emuspeed != EMU_SPEED_PAUSED && emuspeed != EMU_SPEED_FULL)
        al_start_timer(timer);
}

void main_setquit(void)
{
    quitting = 1;
}

int main(int argc, char **argv)
{
    main_init(argc, argv);
    main_run();
    main_close();
    return 0;
}

char* strreplace(char* s, const char* s1, const char* s2) {
    char* p = strstr(s, s1);
    if (p != NULL) {
        size_t len1 = strlen(s1);
        size_t len2 = strlen(s2);
        if (len1 != len2)
            memmove(p + len2, p + len1, strlen(p + len1) + 1);
        memcpy(p, s2, len2);
    }
    return s;
}
