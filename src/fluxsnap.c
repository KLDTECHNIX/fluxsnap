#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>

#define DEFAULT_GAP 10
#define MAX_MANAGED 1024
#define MAX_MONITORS 16

typedef struct {
    unsigned int modifier;
    KeySym trigger_key;
    char trigger_key_name[64];
    int gap;
} Config;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    bool valid;
} Rect;

typedef struct {
    Rect area;
    Window windows[MAX_MANAGED];
    int count;
} MonitorBucket;

typedef struct {
    Display *dpy;
    int screen;
    Window root;
    Atom atom_wm_state;
    Atom atom_net_workarea;
    Atom atom_net_current_desktop;
    Atom atom_net_client_list;
    Atom atom_net_wm_window_type;
    Atom atom_net_wm_window_type_dock;
    Atom atom_net_moveresize_window;
    Atom atom_net_wm_state;
    Atom atom_net_wm_state_max_horz;
    Atom atom_net_wm_state_max_vert;
    Config config;
} App;

static int g_grab_badaccess = 0;

static int xerr_grab_handler(Display *dpy, XErrorEvent *ev) {
    (void)dpy;
    if (ev->error_code == BadAccess && ev->request_code == 33) {
        g_grab_badaccess = 1;
    }
    return 0;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static bool parse_modifier(const char *value, unsigned int *mod) {
    if (strcasecmp(value, "Mod4") == 0 || strcasecmp(value, "Super") == 0 || strcasecmp(value, "Win") == 0) {
        *mod = Mod4Mask;
        return true;
    }
    if (strcasecmp(value, "Mod1") == 0 || strcasecmp(value, "Alt") == 0) {
        *mod = Mod1Mask;
        return true;
    }
    if (strcasecmp(value, "Control") == 0 || strcasecmp(value, "Ctrl") == 0) {
        *mod = ControlMask;
        return true;
    }
    if (strcasecmp(value, "Shift") == 0) {
        *mod = ShiftMask;
        return true;
    }
    return false;
}

static void set_default_config(Config *cfg) {
    cfg->modifier = Mod4Mask;
    cfg->trigger_key = XK_space;
    snprintf(cfg->trigger_key_name, sizeof(cfg->trigger_key_name), "space");
    cfg->gap = DEFAULT_GAP;
}

static void load_config_file(Config *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    unsigned int parsed_mod;
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = trim(p);
        char *value = trim(eq + 1);

        if (strcasecmp(key, "modifier") == 0 && parse_modifier(value, &parsed_mod)) {
            cfg->modifier = parsed_mod;
        } else if (strcasecmp(key, "hotkey") == 0) {
            KeySym ks = XStringToKeysym(value);
            if (ks != NoSymbol) {
                cfg->trigger_key = ks;
                snprintf(cfg->trigger_key_name, sizeof(cfg->trigger_key_name), "%s", value);
            }
        } else if (strcasecmp(key, "gap") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 0 && v <= 300) cfg->gap = (int)v;
        }
    }
    fclose(f);
}

static void load_config(Config *cfg, const char *explicit_path) {
    set_default_config(cfg);

    if (explicit_path) {
        load_config_file(cfg, explicit_path);
        return;
    }

    char path[PATH_MAX];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(path, sizeof(path), "%s/fluxsnap/config", xdg);
        load_config_file(cfg, path);
        return;
    }

    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(path, sizeof(path), "%s/.config/fluxsnap/config", home);
        load_config_file(cfg, path);
        return;
    }

    load_config_file(cfg, "/usr/local/etc/fluxsnap.conf");
}

static bool root_cardinal(App *app, Atom property, unsigned long **out, unsigned long *count) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(app->dpy,
                           app->root,
                           property,
                           0,
                           4096,
                           False,
                           XA_CARDINAL,
                           &actual_type,
                           &actual_format,
                           &nitems,
                           &bytes_after,
                           &prop) != Success) {
        return false;
    }

    if (!prop || actual_type != XA_CARDINAL || actual_format != 32 || nitems == 0) {
        if (prop) XFree(prop);
        return false;
    }

    *out = (unsigned long *)prop;
    *count = nitems;
    return true;
}

static Rect get_workarea(App *app) {
    Rect wa = {0, 0, DisplayWidth(app->dpy, app->screen), DisplayHeight(app->dpy, app->screen), true};

    unsigned long *workareas = NULL;
    unsigned long wa_count = 0;
    if (!root_cardinal(app, app->atom_net_workarea, &workareas, &wa_count)) return wa;

    unsigned long desktop = 0;
    unsigned long *desktop_prop = NULL;
    unsigned long desktop_count = 0;
    if (root_cardinal(app, app->atom_net_current_desktop, &desktop_prop, &desktop_count)) {
        desktop = desktop_prop[0];
        XFree(desktop_prop);
    }

    unsigned long areas = wa_count / 4;
    if (areas == 0) {
        XFree(workareas);
        return wa;
    }
    if (desktop >= areas) desktop = 0;

    unsigned long idx = desktop * 4;
    wa.x = (int)workareas[idx];
    wa.y = (int)workareas[idx + 1];
    wa.width = (int)workareas[idx + 2];
    wa.height = (int)workareas[idx + 3];
    XFree(workareas);

    if (wa.width <= 0 || wa.height <= 0) {
        wa = (Rect){0, 0, DisplayWidth(app->dpy, app->screen), DisplayHeight(app->dpy, app->screen), true};
    }
    return wa;
}

static bool window_has_atom(App *app, Window w, Atom prop, Atom expected) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(app->dpy,
                           w,
                           prop,
                           0,
                           32,
                           False,
                           XA_ATOM,
                           &actual_type,
                           &actual_format,
                           &nitems,
                           &bytes_after,
                           &data) != Success) {
        return false;
    }

    bool found = false;
    if (data && actual_type == XA_ATOM && actual_format == 32) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == expected) {
                found = true;
                break;
            }
        }
    }
    if (data) XFree(data);
    return found;
}

static bool is_normal_window(App *app, Window w) {
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(app->dpy, w, &attrs)) return false;
    if (attrs.override_redirect || attrs.map_state != IsViewable) return false;

    if (window_has_atom(app, w, app->atom_net_wm_window_type, app->atom_net_wm_window_type_dock)) {
        return false;
    }

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    int rc = XGetWindowProperty(app->dpy,
                                w,
                                app->atom_wm_state,
                                0,
                                0,
                                False,
                                AnyPropertyType,
                                &actual_type,
                                &actual_format,
                                &nitems,
                                &bytes_after,
                                &prop);
    if (prop) XFree(prop);
    return rc == Success && actual_type != None;
}

#ifdef HAVE_XINERAMA
static Rect rect_intersection(const Rect *a, const Rect *b) {
    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2 = ((a->x + a->width) < (b->x + b->width)) ? (a->x + a->width) : (b->x + b->width);
    int y2 = ((a->y + a->height) < (b->y + b->height)) ? (a->y + a->height) : (b->y + b->height);
    if (x2 <= x1 || y2 <= y1) return (Rect){0};
    return (Rect){x1, y1, x2 - x1, y2 - y1, true};
}
#endif

static int get_visible_monitors(App *app, Rect workarea, Rect out[MAX_MONITORS]) {
    int n = 0;

#ifndef HAVE_XINERAMA
    (void)app;
#else
    int evb, erb;
    if (XineramaQueryExtension(app->dpy, &evb, &erb) && XineramaIsActive(app->dpy)) {
        int xcount = 0;
        XineramaScreenInfo *xs = XineramaQueryScreens(app->dpy, &xcount);
        if (xs && xcount > 0) {
            for (int i = 0; i < xcount && n < MAX_MONITORS; i++) {
                Rect mon = {xs[i].x_org, xs[i].y_org, xs[i].width, xs[i].height, true};
                Rect clipped = rect_intersection(&workarea, &mon);
                if (clipped.valid) out[n++] = clipped;
            }
            XFree(xs);
        }
    }
#endif

    if (n == 0) out[n++] = workarea;
    return n;
}

static int monitor_index_for_point(const Rect mons[], int nmon, int x, int y) {
    for (int i = 0; i < nmon; i++) {
        if (x >= mons[i].x && x < mons[i].x + mons[i].width && y >= mons[i].y && y < mons[i].y + mons[i].height) {
            return i;
        }
    }
    return 0;
}

static Window frame_window_for_client(App *app, Window client) {
    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(app->dpy, client, &root_ret, &parent_ret, &children, &nchildren)) return client;
    if (children) XFree(children);
    if (parent_ret != None && parent_ret != app->root) return parent_ret;
    return client;
}

static void clear_maximized_state(App *app, Window w) {
    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = app->atom_net_wm_state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 0;
    ev.xclient.data.l[1] = app->atom_net_wm_state_max_horz;
    ev.xclient.data.l[2] = app->atom_net_wm_state_max_vert;
    ev.xclient.data.l[3] = 2;
    XSendEvent(app->dpy, app->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

static void apply_rect(App *app, Window w, Rect r) {
    clear_maximized_state(app, w);

    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.message_type = app->atom_net_moveresize_window;
    ev.xclient.window = w;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = NorthWestGravity | (1L << 8) | (1L << 9) | (1L << 10) | (1L << 11);
    ev.xclient.data.l[1] = r.x;
    ev.xclient.data.l[2] = r.y;
    ev.xclient.data.l[3] = r.width;
    ev.xclient.data.l[4] = r.height;
    XSendEvent(app->dpy, app->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);

    Window frame = frame_window_for_client(app, w);
    XMoveResizeWindow(app->dpy, frame, r.x, r.y, (unsigned int)r.width, (unsigned int)r.height);
}

static int load_client_list(App *app, Window out[MAX_MANAGED]) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(app->dpy,
                           app->root,
                           app->atom_net_client_list,
                           0,
                           MAX_MANAGED,
                           False,
                           XA_WINDOW,
                           &actual_type,
                           &actual_format,
                           &nitems,
                           &bytes_after,
                           &data) != Success) {
        return 0;
    }

    int count = 0;
    if (data && actual_type == XA_WINDOW && actual_format == 32) {
        Window *wins = (Window *)data;
        for (unsigned long i = 0; i < nitems && count < MAX_MANAGED; i++) {
            if (is_normal_window(app, wins[i])) out[count++] = wins[i];
        }
    }
    if (data) XFree(data);
    return count;
}

static void layout_bucket(App *app, MonitorBucket *b) {
    int n = b->count;
    if (n <= 0) return;

    int gap = app->config.gap;
    Rect a = b->area;

    int inner_x = gap;
    int inner_y = gap;

    int columns = (n <= 3) ? n : 3;
    int usable_w = a.width - (2 * gap) - ((columns - 1) * inner_x);
    if (usable_w < columns) usable_w = columns;

    int col_w[3] = {0, 0, 0};
    for (int c = 0; c < columns; c++) {
        col_w[c] = usable_w / columns + (c < (usable_w % columns) ? 1 : 0);
    }

    int col_x[3] = {a.x + gap, 0, 0};
    for (int c = 1; c < columns; c++) {
        col_x[c] = col_x[c - 1] + col_w[c - 1] + inner_x;
    }

    Window cols[3][MAX_MANAGED];
    int ncol[3] = {0, 0, 0};

    for (int i = 0; i < n; i++) {
        int c = (n <= 3) ? i : (i % 3);
        cols[c][ncol[c]++] = b->windows[i];
    }

    for (int c = 0; c < columns; c++) {
        if (ncol[c] == 0) continue;

        int usable_h = a.height - (2 * gap) - ((ncol[c] - 1) * inner_y);
        if (usable_h < ncol[c]) usable_h = ncol[c];

        int y = a.y + gap;
        int base_h = usable_h / ncol[c];
        int rem = usable_h % ncol[c];

        for (int i = 0; i < ncol[c]; i++) {
            int h = base_h + (i < rem ? 1 : 0);
            Rect r = {col_x[c], y, col_w[c], h, true};
            apply_rect(app, cols[c][i], r);
            y += h + inner_y;
        }
    }
}

static void tile_all_windows(App *app) {
    Window wins[MAX_MANAGED];
    int count = load_client_list(app, wins);
    if (count <= 0) return;

    Rect wa = get_workarea(app);
    Rect mons[MAX_MONITORS] = {0};
    int nmon = get_visible_monitors(app, wa, mons);

    MonitorBucket buckets[MAX_MONITORS] = {0};
    for (int m = 0; m < nmon; m++) buckets[m].area = mons[m];

    for (int i = 0; i < count; i++) {
        XWindowAttributes attrs;
        Window child;
        int root_x = 0;
        int root_y = 0;
        int mon = 0;

        if (XGetWindowAttributes(app->dpy, wins[i], &attrs)) {
            XTranslateCoordinates(app->dpy,
                                  wins[i],
                                  app->root,
                                  attrs.width / 2,
                                  attrs.height / 2,
                                  &root_x,
                                  &root_y,
                                  &child);
            mon = monitor_index_for_point(mons, nmon, root_x, root_y);
        }

        if (buckets[mon].count < MAX_MANAGED) {
            buckets[mon].windows[buckets[mon].count++] = wins[i];
        }
    }

    for (int m = 0; m < nmon; m++) {
        layout_bucket(app, &buckets[m]);
    }

    XSync(app->dpy, False);
}

static bool grab_hotkey(App *app) {
    const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    KeyCode code = XKeysymToKeycode(app->dpy, app->config.trigger_key);
    if (code == 0) return false;

    int (*old_handler)(Display *, XErrorEvent *) = XSetErrorHandler(xerr_grab_handler);
    g_grab_badaccess = 0;

    for (size_t i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
        XGrabKey(app->dpy,
                 (int)code,
                 app->config.modifier | masks[i],
                 app->root,
                 True,
                 GrabModeAsync,
                 GrabModeAsync);
    }

    XSync(app->dpy, False);
    XSetErrorHandler(old_handler);
    return g_grab_badaccess == 0;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-c /path/to/config]\n", prog);
}

int main(int argc, char **argv) {
    const char *config_path = NULL;
    int ch;

    while ((ch = getopt(argc, argv, "c:h")) != -1) {
        switch (ch) {
            case 'c':
                config_path = optarg;
                break;
            case 'h':
            default:
                usage(argv[0]);
                return (ch == 'h') ? 0 : 1;
        }
    }

    App app = {0};
    load_config(&app.config, config_path);

    app.dpy = XOpenDisplay(NULL);
    if (!app.dpy) {
        fprintf(stderr, "fluxsnap: cannot open X display\n");
        return 1;
    }

    app.screen = DefaultScreen(app.dpy);
    app.root = RootWindow(app.dpy, app.screen);

    app.atom_wm_state = XInternAtom(app.dpy, "WM_STATE", False);
    app.atom_net_workarea = XInternAtom(app.dpy, "_NET_WORKAREA", False);
    app.atom_net_current_desktop = XInternAtom(app.dpy, "_NET_CURRENT_DESKTOP", False);
    app.atom_net_client_list = XInternAtom(app.dpy, "_NET_CLIENT_LIST", False);
    app.atom_net_wm_window_type = XInternAtom(app.dpy, "_NET_WM_WINDOW_TYPE", False);
    app.atom_net_wm_window_type_dock = XInternAtom(app.dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    app.atom_net_moveresize_window = XInternAtom(app.dpy, "_NET_MOVERESIZE_WINDOW", False);
    app.atom_net_wm_state = XInternAtom(app.dpy, "_NET_WM_STATE", False);
    app.atom_net_wm_state_max_horz = XInternAtom(app.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    app.atom_net_wm_state_max_vert = XInternAtom(app.dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);

    XSelectInput(app.dpy, app.root, SubstructureNotifyMask | KeyPressMask);
    if (!grab_hotkey(&app)) {
        fprintf(stderr,
                "fluxsnap: hotkey %s+%s is already grabbed by another program/window manager\n",
                (app.config.modifier == Mod4Mask) ? "Super" :
                (app.config.modifier == Mod1Mask) ? "Alt" :
                (app.config.modifier == ControlMask) ? "Ctrl" : "Shift",
                app.config.trigger_key_name);
        fprintf(stderr, "fluxsnap: change modifier/hotkey in config or unbind the key in Fluxbox\n");
        return 1;
    }

    for (;;) {
        XEvent ev;
        XNextEvent(app.dpy, &ev);

        if (ev.type == KeyPress) {
            KeySym sym = XkbKeycodeToKeysym(app.dpy, ev.xkey.keycode, 0, 0);
            if (sym == app.config.trigger_key && (ev.xkey.state & app.config.modifier)) {
                tile_all_windows(&app);
            }
        } else if (ev.type == MapNotify) {
            if (ev.xmap.event == app.root && is_normal_window(&app, ev.xmap.window)) {
                tile_all_windows(&app);
            }
        }
    }

    return 0;
}
