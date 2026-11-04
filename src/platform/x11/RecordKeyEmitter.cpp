// Qt first: X11 headers define Status/None/True/False.

#include "RecordKeyEmitter.h"

#include <X11/Xlib.h>
#include <X11/Xproto.h>          // xEvent (RECORD payload)
#include <X11/extensions/record.h>
#include <X11/keysym.h>

#include <cstdio>

namespace lt {
namespace {

// Keycode -> keysym map, built on a *separate* connection before RECORD is
// enabled. Inside an XRecordEnableContext callback the data connection is
// owned by the record read loop: calling Xlib (e.g. XKeycodeToKeysym) on it
// deadlocks. Reading this table is pure memory and safe.
unsigned long g_keysym[256] = {0};

void intercept(XPointer, XRecordInterceptData *data)
{
    if (data->category == XRecordFromServer && data->data_len > 0) {
        const auto *ev = reinterpret_cast<const xEvent *>(data->data);
        const int type = ev->u.u.type;
        if (type == KeyPress || type == KeyRelease) {
            const unsigned code = ev->u.u.detail;
            std::printf("%c %lu %u\n", type == KeyPress ? 'K' : 'R',
                        g_keysym[code & 0xff], code);
            std::fflush(stdout);
        }
    }
    XRecordFreeData(data);
}

} // namespace

int runRecordKeyEmitter()
{
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        return 1;
    }
    int major = 0, minor = 0;
    if (!XRecordQueryVersion(dpy, &major, &minor)) {
        return 1;
    }
    XRecordRange *range = XRecordAllocRange();
    if (!range) {
        return 1;
    }
    range->device_events.first = KeyPress;
    range->device_events.last = KeyRelease;
    XRecordClientSpec clients = XRecordAllClients;
    XRecordContext ctx = XRecordCreateContext(dpy, 0, &clients, 1, &range, 1);
    XFree(range);
    if (!ctx) {
        return 1;
    }

    // Snapshot the keyboard map on an independent connection while the data
    // connection is still free; close it before entering the record loop.
    if (Display *map = XOpenDisplay(nullptr)) {
        for (int kc = 8; kc < 256; ++kc) {
            g_keysym[kc] = XKeycodeToKeysym(map, kc, 0);
        }
        XCloseDisplay(map);
    }

    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    XRecordEnableContext(dpy, ctx, &intercept, nullptr);
    return 0;
}

} // namespace lt
