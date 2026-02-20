#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <unistd.h>

#define DEFAULT_GAP 16
#define MAX_MANAGED 1024

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
    Window w;
    int preferred_col; /* -1 = auto */
} Pref;

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
    Pref prefs[MAX_MANAGED];
    int pref_count;
    bool awaiting_pick;
} App;

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

    if (window_has_atom(app, w, app->atom_net_wm_window_type, app->atom_net_wm_window_type_dock)) return false;

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

static int get_pref_index(App *app, Window w) {
    for (int i = 0; i < app->pref_count; i++) {
        if (app->prefs[i].w == w) return i;
    }
    return -1;
}

static int get_pref_col(App *app, Window w) {
    int idx = get_pref_index(app, w);
    if (idx < 0) return -1;
    return app->prefs[idx].preferred_col;
}

static void set_pref_col(App *app, Window w, int col) {
    int idx = get_pref_index(app, w);
    if (idx >= 0) {
        app->prefs[idx].preferred_col = col;
        return;
    }
    if (app->pref_count >= MAX_MANAGED) return;
    app->prefs[app->pref_count].w = w;
    app->prefs[app->pref_count].preferred_col = col;
    app->pref_count++;
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

static void apply_rect(App *app, Window w, const Rect *r) {
    clear_maximized_state(app, w);

    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.message_type = app->atom_net_moveresize_window;
    ev.xclient.window = w;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = NorthWestGravity | (1L << 8) | (1L << 9) | (1L << 10) | (1L << 11);
    ev.xclient.data.l[1] = r->x;
    ev.xclient.data.l[2] = r->y;
    ev.xclient.data.l[3] = r->width;
    ev.xclient.data.l[4] = r->height;

    XSendEvent(app->dpy, app->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XMoveResizeWindow(app->dpy, w, r->x, r->y, (unsigned int)r->width, (unsigned int)r->height);
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

static void tile_all_windows(App *app) {
    Window windows[MAX_MANAGED];
    int count = load_client_list(app, windows);
    if (count <= 0) return;

    Rect wa = get_workarea(app);
    int g = app->config.gap;
    int inner = g;

    int usable_w = wa.width - (2 * g) - (2 * inner);
    if (usable_w < 3) usable_w = 3;

    int col_w[3] = {usable_w / 3, usable_w / 3, usable_w - (usable_w / 3) * 2};
    int col_x[3];
    col_x[0] = wa.x + g;
    col_x[1] = col_x[0] + col_w[0] + inner;
    col_x[2] = col_x[1] + col_w[1] + inner;

    Window cols[3][MAX_MANAGED];
    int ncol[3] = {0, 0, 0};

    for (int i = 0; i < count; i++) {
        int pref = get_pref_col(app, windows[i]);
        int target_col = pref;
        if (target_col < 0 || target_col > 2) {
            target_col = 0;
            if (ncol[1] < ncol[target_col]) target_col = 1;
            if (ncol[2] < ncol[target_col]) target_col = 2;
        }
        cols[target_col][ncol[target_col]++] = windows[i];
    }

    int usable_h_base = wa.height - (2 * g);
    for (int c = 0; c < 3; c++) {
        if (ncol[c] == 0) continue;

        int usable_h = usable_h_base - ((ncol[c] - 1) * inner);
        if (usable_h < ncol[c]) usable_h = ncol[c];

        int y = wa.y + g;
        int base_h = usable_h / ncol[c];
        int rem = usable_h % ncol[c];

        for (int i = 0; i < ncol[c]; i++) {
            int h = base_h + (i < rem ? 1 : 0);
            Rect r = {col_x[c], y, col_w[c], h, true};
            apply_rect(app, cols[c][i], &r);
            y += h + inner;
        }
    }

    XSync(app->dpy, False);
}

static int menu_pick_column(App *app, int root_x, int root_y) {
    const int w = 210;
    const int h = 42;
    int sw = DisplayWidth(app->dpy, app->screen);
    int sh = DisplayHeight(app->dpy, app->screen);

    if (root_x + w > sw) root_x = sw - w;
    if (root_y + h > sh) root_y = sh - h;
    if (root_x < 0) root_x = 0;
    if (root_y < 0) root_y = 0;

    XSetWindowAttributes a;
    a.override_redirect = True;
    a.background_pixel = 0x222222;
    a.border_pixel = 0xffffff;

    Window menu = XCreateWindow(app->dpy,
                                app->root,
                                root_x,
                                root_y,
                                (unsigned int)w,
                                (unsigned int)h,
                                1,
                                CopyFromParent,
                                InputOutput,
                                CopyFromParent,
                                CWOverrideRedirect | CWBackPixel | CWBorderPixel,
                                &a);
    XSelectInput(app->dpy, menu, ExposureMask | ButtonPressMask);
    XMapRaised(app->dpy, menu);

    GC gc = DefaultGC(app->dpy, app->screen);
    int choice = -1;

    for (;;) {
        XEvent ev;
        XWindowEvent(app->dpy, menu, ExposureMask | ButtonPressMask, &ev);

        if (ev.type == Expose) {
            XSetForeground(app->dpy, gc, 0xdddddd);
            XDrawLine(app->dpy, menu, gc, w / 3, 0, w / 3, h);
            XDrawLine(app->dpy, menu, gc, (w * 2) / 3, 0, (w * 2) / 3, h);
            XDrawString(app->dpy, menu, gc, 22, 25, "Left", 4);
            XDrawString(app->dpy, menu, gc, 88, 25, "Middle", 6);
            XDrawString(app->dpy, menu, gc, 163, 25, "Right", 5);
        } else if (ev.type == ButtonPress) {
            int x = ev.xbutton.x;
            if (x < w / 3) choice = 0;
            else if (x < (w * 2) / 3) choice = 1;
            else choice = 2;
            break;
        }
    }

    XDestroyWindow(app->dpy, menu);
    XSync(app->dpy, False);
    return choice;
}

static void maybe_pick_window_column(App *app) {
    if (!app->awaiting_pick) return;

    app->awaiting_pick = false;
    XGrabPointer(app->dpy,
                 app->root,
                 True,
                 ButtonPressMask,
                 GrabModeAsync,
                 GrabModeAsync,
                 None,
                 None,
                 CurrentTime);

    XEvent ev;
    XMaskEvent(app->dpy, ButtonPressMask, &ev);

    XUngrabPointer(app->dpy, CurrentTime);

    Window candidate = ev.xbutton.subwindow;
        if (candidate == None || !is_normal_window(app, candidate)) return;

    int pick = menu_pick_column(app, ev.xbutton.x_root, ev.xbutton.y_root);
    if (pick >= 0) {
        set_pref_col(app, candidate, pick);
        tile_all_windows(app);
    }
}

static void grab_hotkey(App *app) {
    const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    KeyCode code = XKeysymToKeycode(app->dpy, app->config.trigger_key);
    if (code == 0) return;

    for (size_t i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
        XGrabKey(app->dpy,
                 (int)code,
                 app->config.modifier | masks[i],
                 app->root,
                 True,
                 GrabModeAsync,
                 GrabModeAsync);
    }
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
    grab_hotkey(&app);
    XSync(app.dpy, False);

    for (;;) {
        XEvent ev;
        XNextEvent(app.dpy, &ev);

        if (ev.type == KeyPress) {
            KeySym sym = XkbKeycodeToKeysym(app.dpy, ev.xkey.keycode, 0, 0);
            if (sym == app.config.trigger_key && (ev.xkey.state & app.config.modifier)) {
                tile_all_windows(&app);
                app.awaiting_pick = true;
                maybe_pick_window_column(&app);
            }
        } else if (ev.type == MapNotify) {
            if (ev.xmap.event == app.root && is_normal_window(&app, ev.xmap.window)) {
                tile_all_windows(&app);
            }
        } else if (ev.type == DestroyNotify) {
            for (int i = 0; i < app.pref_count; i++) {
                if (app.prefs[i].w == ev.xdestroywindow.window) {
                    app.prefs[i] = app.prefs[app.pref_count - 1];
                    app.pref_count--;
                    break;
                }
            }
        }
    }

    return 0;
}
