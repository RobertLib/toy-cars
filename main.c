#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "game.h"
#include "renderer.h"
#include "ui.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {SDL_AtomicInt background;} Lifecycle;
static bool SDLCALL lifecycle_watch(void *data,SDL_Event *e){
    Lifecycle *l=data;
    if(e->type==SDL_EVENT_WILL_ENTER_BACKGROUND)SDL_SetAtomicInt(&l->background,1);
    if(e->type==SDL_EVENT_DID_ENTER_FOREGROUND)SDL_SetAtomicInt(&l->background,2);
    return true;
}
static void show_error(const char *message){SDL_Log("%s: %s",message,SDL_GetError());SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"ToyCars",message,NULL);}
static SDL_Gamepad *open_gamepad(void){int count;SDL_JoystickID *ids=SDL_GetGamepads(&count);SDL_Gamepad *p=NULL;for(int i=0;i<count&&!p;i++)p=SDL_OpenGamepad(ids[i]);SDL_free(ids);return p;}
static void toggle_pause(Game *g,UI *u){if(g->screen==SCREEN_RACE||g->screen==SCREEN_COUNTDOWN)g->screen=SCREEN_PAUSE;else if(g->screen==SCREEN_PAUSE)g->screen=g->countdown>0?SCREEN_COUNTDOWN:SCREEN_RACE;else g->screen=SCREEN_MENU;ui_clear_input(u);}

int main(int argc,char **argv){
    bool test=false,autoplay=false,noaudio=false,benchmark=false,touch_override=false;int track=0,frames=0,width=1440,height=900;const char *screenshot=NULL,*screen=NULL;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--test"))test=true;
        else if(!strcmp(argv[i],"--autoplay"))autoplay=true;
        else if(!strcmp(argv[i],"--no-audio"))noaudio=true;
        else if(!strcmp(argv[i],"--benchmark"))benchmark=true;
        else if(!strcmp(argv[i],"--touch"))touch_override=true;
        else if(!strcmp(argv[i],"--track")&&i+1<argc){i++;track=!strcmp(argv[i],"beach")?1:!strcmp(argv[i],"winter")?2:0;}
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)screenshot=argv[++i];
        else if(!strcmp(argv[i],"--screen")&&i+1<argc)screen=argv[++i];
        else if(!strcmp(argv[i],"--size")&&i+1<argc){i++;if(sscanf(argv[i],"%dx%d",&width,&height)!=2||width<480||height<320){fprintf(stderr,"Invalid window size\n");return 1;}}
        else if(!strcmp(argv[i],"--help")){printf("ToyCars\n  --test                     Run deterministic simulation tests (no window)\n  --track country|beach|winter\n  --screen menu|race|garage|help|settings|results\n  --autoplay                 Drive the player with AI for a demo\n  --frames N                 Exit after N rendered frames\n  --screenshot file.bmp      Capture the last frame\n  --size WIDTHxHEIGHT        Window size\n  --benchmark                Disable vsync; fixed frame delta\n  --no-audio                 Do not open an audio device\n  --touch                    Enable touch controls for this session\n");return 0;}
        else{fprintf(stderr,"Unknown or incomplete option: %s\n",argv[i]);return 1;}
    }
    if(test){if(!SDL_Init(0))return 1;int code=game_tests();code|=ui_tests();SDL_Quit();return code;}
    SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|(noaudio?0:SDL_INIT_AUDIO))){SDL_Log("SDL: %s",SDL_GetError());return 1;}
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
    SDL_WindowFlags flags=SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    flags|=SDL_WINDOW_FULLSCREEN;
#endif
    SDL_Window *window=SDL_CreateWindow("ToyCars - The Little Rally Club",width,height,flags);
    if(!window){show_error("Could not open the game window.");SDL_Quit();return 1;}
    SDL_SetWindowMinimumSize(window,800,450);
    SDL_GLContext context=SDL_GL_CreateContext(window);
    if(!context){show_error("ToyCars needs OpenGL 3.3 or OpenGL ES 3.0.");SDL_DestroyWindow(window);SDL_Quit();return 1;}
    SDL_GL_SetSwapInterval(benchmark?0:1);
    Game *g=calloc(1,sizeof *g);Renderer *r=calloc(1,sizeof *r);UI *u=calloc(1,sizeof *u);Audio audio={0};
    if(!g||!r||!u||!game_load(g)||!renderer_init(r)||!ui_init(u)){show_error("The game assets could not be loaded. Keep the assets folder beside ToyCars.");free(g);free(r);free(u);SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();return 1;}
    g->selected=track;if(touch_override)g->profile.touch=true;
    g->test_mode=autoplay||frames>0||screen!=NULL;
    if(screen){
        if(!strcmp(screen,"race")){game_start(g);}
        else if(!strcmp(screen,"garage"))g->screen=SCREEN_GARAGE;
        else if(!strcmp(screen,"help"))g->screen=SCREEN_HELP;
        else if(!strcmp(screen,"settings"))g->screen=SCREEN_SETTINGS;
        else if(!strcmp(screen,"results")){
            g->test_mode=true;game_start(g);for(int i=0;i<120*200&&g->screen!=SCREEN_RESULTS;i++)game_tick(g,(Controls){0},FIXED_DT,true);
        }
    }else if(autoplay)game_start(g);
    if(!noaudio)audio_init(&audio);
    SDL_Gamepad *pad=open_gamepad();SDL_Cursor *pointer=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER),*normal=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    Lifecycle life={0};SDL_AddEventWatch(lifecycle_watch,&life);
    bool running=true,background=false;int exit_code=0;double accumulator=0;Uint64 previous=SDL_GetTicksNS();int frame=0;double elapsed=0;float fps_acc=0;int fps_frames=0;
    SDL_Log("ToyCars | %s | %s",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    while(running){
        Uint64 now=SDL_GetTicksNS();float dt=(float)((now-previous)/1e9);previous=now;dt=clampf(dt,0,.1f);if(benchmark)dt=1/60.f;
        int w,h,ww,wh;SDL_GetWindowSizeInPixels(window,&w,&h);SDL_GetWindowSize(window,&ww,&wh);if(w<=0||h<=0){SDL_Delay(20);continue;}
        ui_size(u,w,h,window);r->ui_width=u->width;r->ui_height=u->height;
        int life_event=SDL_SetAtomicInt(&life.background,0);
        if(life_event){
            background=life_event==1;if(background&&(g->screen==SCREEN_RACE||g->screen==SCREEN_COUNTDOWN))g->screen=SCREEN_PAUSE;
            ui_clear_input(u);audio_pause(&audio,background);accumulator=0;dt=0;
        }
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            ui_event(u,&e,ww,wh);
            switch(e.type){
                case SDL_EVENT_QUIT:running=false;break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    if(g->screen==SCREEN_RACE||g->screen==SCREEN_COUNTDOWN)g->screen=SCREEN_PAUSE;ui_clear_input(u);break;
                case SDL_EVENT_GAMEPAD_ADDED:if(!pad)pad=SDL_OpenGamepad(e.gdevice.which);break;
                case SDL_EVENT_GAMEPAD_REMOVED:if(pad&&SDL_GetGamepadID(pad)==e.gdevice.which){SDL_CloseGamepad(pad);pad=NULL;}break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    if(e.gbutton.button==SDL_GAMEPAD_BUTTON_START)toggle_pause(g,u);
                    if(e.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH&&(g->screen==SCREEN_MENU||g->screen==SCREEN_RESULTS))game_start(g);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if(e.key.repeat)break;
                    if(e.key.key==SDLK_ESCAPE||e.key.key==SDLK_AC_BACK)toggle_pause(g,u);
                    if(e.key.key==SDLK_RETURN){if(g->screen==SCREEN_MENU||g->screen==SCREEN_RESULTS)game_start(g);else if(g->screen==SCREEN_PAUSE)g->screen=g->countdown>0?SCREEN_COUNTDOWN:SCREEN_RACE;}
                    if(e.key.key==SDLK_R&&g->screen==SCREEN_RACE)game_reset_car(g,0);
                    if(e.key.key>=SDLK_1&&e.key.key<=SDLK_3&&g->screen==SCREEN_MENU)g->selected=(int)(e.key.key-SDLK_1);
                    if(e.key.key==SDLK_F1)u->show_stats=!u->show_stats;
                    if(e.key.key==SDLK_F2)renderer_screenshot(r,"toycars-capture.bmp");
                    if(e.key.key==SDLK_M){g->profile.sound=!g->profile.sound;g->profile.music=g->profile.sound;profile_save(&g->profile);}
                    if(e.key.key==SDLK_F11)SDL_SetWindowFullscreen(window,!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN));
                    break;
                default:break;
            }
        }
        if(background){SDL_Delay(40);continue;}
        if(wh>ww&&(g->screen==SCREEN_RACE||g->screen==SCREEN_COUNTDOWN))g->screen=SCREEN_PAUSE;
        Controls controls=ui_controls(u,g);
        if(pad&&g->screen==SCREEN_RACE){
            float steer=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX)/32767.f;if(fabsf(steer)>.15f)controls.steer=(fabsf(steer)-.15f)/.85f*(steer<0?-1:1);
            float gas=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)/32767.f,brake=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)/32767.f;
            controls.throttle=fmaxf(controls.throttle,gas);controls.brake=fmaxf(controls.brake,brake);controls.drift|=SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_SOUTH);
            if(controls.brake>.15f)controls.throttle=0;
        }
        accumulator+=dt;int steps=0;
        while(accumulator>=FIXED_DT&&steps++<12){game_tick(g,controls,FIXED_DT,autoplay);accumulator-=FIXED_DT;}
        if(steps>=12)accumulator=fmin(accumulator,FIXED_DT);
        r->default_fbo=(GLuint)SDL_GetNumberProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER,0);
        renderer_resize(r,w,h);renderer_draw(r,g,dt,(float)(accumulator/FIXED_DT));ui_draw(u,g,r);
        audio_update(&audio,g);SDL_SetCursor(u->hovered?pointer:normal);
        frame++;elapsed+=dt;fps_acc+=dt;fps_frames++;if(fps_acc>.5f){u->fps=fps_frames/fps_acc;fps_acc=0;fps_frames=0;}
        GLenum error=glGetError();if(error!=GL_NO_ERROR){SDL_Log("OpenGL error 0x%x at frame %d",error,frame);running=false;exit_code=1;}
        if(frames>0&&frame>=frames){if(screenshot){bool ok=renderer_screenshot(r,screenshot);SDL_Log("Screenshot %s: %s",screenshot,ok?"saved":"failed");if(!ok)exit_code=1;}running=false;}
#ifdef SDL_PLATFORM_IOS
        glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)SDL_GetNumberProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_UIKIT_OPENGL_RENDERBUFFER_NUMBER,0));
#endif
        if(!SDL_GL_SwapWindow(window)){SDL_Log("Present failed: %s",SDL_GetError());exit_code=1;running=false;}
    }
    SDL_Log("Session: %d frames, %.2f game seconds, screen=%d, race=%.2fs, fuel=%.1f, position=%d",frame,elapsed,g->screen,g->time,g->cars[0].fuel,g->rank);
    SDL_RemoveEventWatch(lifecycle_watch,&life);if(pad)SDL_CloseGamepad(pad);SDL_DestroyCursor(pointer);SDL_DestroyCursor(normal);audio_destroy(&audio);ui_destroy(u);renderer_destroy(r);free(u);free(r);free(g);SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();return exit_code;
}
