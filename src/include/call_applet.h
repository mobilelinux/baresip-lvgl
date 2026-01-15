#ifndef CALL_APPLET_H
#define CALL_APPLET_H

#include "applet.h"

extern applet_t call_applet;

void call_applet_open(const char *number);
void call_applet_video_open(const char *number);
void call_applet_request_incoming_view(void);
void call_applet_request_active_view(void);

#endif // CALL_APPLET_H
