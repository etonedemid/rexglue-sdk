/**
 * @file        sdl3_android_stubs.c
 * @brief       Stubs for SDL3 Android video/event symbols.
 *
 * SDL3's core Android JNI layer (SDL_android.c) references video, event,
 * and input symbols even when SDL_VIDEO is disabled. Since reNut uses its
 * own Vulkan surface and JNI input bridge, we provide no-op stubs so the
 * linker is satisfied.
 */

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_touch.h>
#include <SDL3/SDL_pen.h>

/* SDL_androidwindow.h — global window pointer */
SDL_Window *Android_Window = NULL;

/* SDL_androidvideo.h */
void Android_SetScreenResolution(int surfaceWidth, int surfaceHeight,
                                  int deviceWidth, int deviceHeight,
                                  float density, float rate) {
    (void)surfaceWidth; (void)surfaceHeight;
    (void)deviceWidth;  (void)deviceHeight;
    (void)density;      (void)rate;
}

void Android_SetOrientation(SDL_DisplayOrientation orientation) {
    (void)orientation;
}

void Android_SendResize(SDL_Window *window) {
    (void)window;
}

void Android_SetWindowSafeAreaInsets(int left, int right, int top, int bottom) {
    (void)left; (void)right; (void)top; (void)bottom;
}

void Android_SetDarkMode(bool enabled) {
    (void)enabled;
}

/* SDL_androidevents.h */
void Android_InitEvents(void) {}
void Android_PumpEvents(Sint64 timeoutNS) { (void)timeoutNS; }
void Android_QuitEvents(void) {}

/* SDL_androidkeyboard.h */
void Android_OnKeyDown(int keycode) { (void)keycode; }
void Android_OnKeyUp(int keycode) { (void)keycode; }
void Android_RestoreScreenKeyboard(void *_this, SDL_Window *window) {
    (void)_this; (void)window;
}

/* SDL_androidmouse.h */
void Android_OnMouse(SDL_Window *window, int button, int action,
                     float x, float y, bool relative) {
    (void)window; (void)button; (void)action;
    (void)x;      (void)y;      (void)relative;
}

/* SDL_androidpen.h */
void Android_OnPen(SDL_Window *window, int pen_id_in,
                   SDL_PenDeviceType device_type, int button, int action,
                   float x, float y, float p) {
    (void)window; (void)pen_id_in; (void)device_type;
    (void)button; (void)action;
    (void)x;      (void)y;         (void)p;
}

/* SDL_androidtouch.h */
void Android_OnTouch(SDL_Window *window, int touch_device_id_in,
                     int pointer_finger_id_in, int action,
                     float x, float y, float p) {
    (void)window; (void)touch_device_id_in; (void)pointer_finger_id_in;
    (void)action; (void)x; (void)y; (void)p;
}

SDL_TouchID Android_ConvertJavaTouchID(int touchID) {
    return (SDL_TouchID)touchID;
}
