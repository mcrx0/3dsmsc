#pragma once
struct SwkbdState {};
enum { SWKBD_TYPE_QWERTY, SWKBD_DEFAULT_QWERTY, SWKBD_BUTTON_CONFIRM };
void swkbdInit(SwkbdState*, int, int, int); void swkbdSetFeatures(SwkbdState*, int); void swkbdSetHintText(SwkbdState*, const char*);
void swkbdSetInitialText(SwkbdState*, const char*); int swkbdInputText(SwkbdState*, char*, unsigned long);
