#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
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
#define MAX_ZONES 16

typedef enum {
    ZONE_ROWS = 0,
    ZONE_COLS = 1,
    ZONE_GRID = 2,
} ZoneLayout;

typedef struct {
    char name[32];
    int x_pct;
    int y_pct;
    int w_pct;
    int h_pct;
    ZoneLayout layout;
    int max_windows; /* 0 == unlimited */
    int gap;
} Zone;

typedef struct {
    unsigned int modifier;
    KeySym trigger_key;
    char trigger_key_name[64];
    int gap;
    int band_top, band_bottom, band_left, band_right;
    Zone zones[MAX_ZONES];
    int zone_count;
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
} ZoneBucket;

typedef struct {
    int left, right, top, bottom;
    int left_start_y, left_end_y;
    int right_start_y, right_end_y;
    int top_start_x, top_end_x;
    int bottom_start_x, bottom_end_x;
} Strut;

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
    Atom atom_net_wm_strut;
    Atom atom_net_wm_strut_partial;
    Config config;
} App;

static int g_grab_badaccess = 0;

static int xerr_grab_handler(Display *dpy, XErrorEvent *ev) {
    (void)dpy;
    if (ev->error_code == BadAccess && ev->request_code == 33) g_grab_badaccess = 1;
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

static ZoneLayout parse_zone_layout(const char *v) {
    if (strcasecmp(v, "cols") == 0) return ZONE_COLS;
    if (strcasecmp(v, "grid") == 0) return ZONE_GRID;
    return ZONE_ROWS;
}

static void set_default_config(Config *cfg) {
    cfg->modifier = Mod4Mask;
    cfg->trigger_key = XK_space;
    snprintf(cfg->trigger_key_name, sizeof(cfg->trigger_key_name), "space");
    cfg->gap = DEFAULT_GAP;
    cfg->zone_count = 3;

    cfg->zones[0] = (Zone){"left", 0, 0, 34, 100, ZONE_ROWS, 0, DEFAULT_GAP};
    cfg->zones[1] = (Zone){"middle", 34, 0, 33, 100, ZONE_ROWS, 0, DEFAULT_GAP};
    cfg->zones[2] = (Zone){"right", 67, 0, 33, 100, ZONE_ROWS, 0, DEFAULT_GAP};
}

static void parse_zone(Config *cfg, const char *value) {
    if (cfg->zone_count >= MAX_ZONES) return;

    char buf[256];
    snprintf(buf, sizeof(buf), "%s", value);

    char *parts[8] = {0};
    int n = 0;
    char *tok = strtok(buf, ",");
    while (tok && n < 8) {
        parts[n++] = trim(tok);
        tok = strtok(NULL, ",");
    }
    if (n < 6) return;

    Zone z = {0};
    snprintf(z.name, sizeof(z.name), "%s", parts[0]);
    z.x_pct = atoi(parts[1]);
    z.y_pct = atoi(parts[2]);
    z.w_pct = atoi(parts[3]);
    z.h_pct = atoi(parts[4]);
    z.layout = parse_zone_layout(parts[5]);
    z.max_windows = (n >= 7) ? atoi(parts[6]) : 0;
    z.gap = (n >= 8) ? atoi(parts[7]) : cfg->gap;

    if (z.x_pct < 0) z.x_pct = 0;
    if (z.y_pct < 0) z.y_pct = 0;
    if (z.w_pct < 1) z.w_pct = 1;
    if (z.h_pct < 1) z.h_pct = 1;
    if (z.gap < 0) z.gap = 0;

    cfg->zones[cfg->zone_count++] = z;
}

static void load_config_file(Config *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    unsigned int parsed_mod;
    bool zone_reset = false;

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
        } else if (strcasecmp(key, "top_band") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 0 && v <= 4000) cfg->band_top = (int)v;
        } else if (strcasecmp(key, "bottom_band") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 0 && v <= 4000) cfg->band_bottom = (int)v;
        } else if (strcasecmp(key, "left_band") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 0 && v <= 4000) cfg->band_left = (int)v;
        } else if (strcasecmp(key, "right_band") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 0 && v <= 4000) cfg->band_right = (int)v;
        } else if (strcasecmp(key, "zone") == 0) {
            if (!zone_reset) {
                cfg->zone_count = 0;
                zone_reset = true;
            }
            parse_zone(cfg, value);
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

/* Read _NET_WM_STRUT_PARTIAL (12 values) or _NET_WM_STRUT (4 values) from a
 * dock window.  Returns false if neither property exists or all values are 0. */
static bool get_window_strut(App *app, Window w, Strut *out) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(app->dpy, w, app->atom_net_wm_strut_partial,
                           0, 12, False, XA_CARDINAL,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &data) == Success
        && data && actual_format == 32 && nitems == 12) {
        unsigned long *v = (unsigned long *)data;
        *out = (Strut){
            (int)v[0],  (int)v[1],  (int)v[2],  (int)v[3],
            (int)v[4],  (int)v[5],  (int)v[6],  (int)v[7],
            (int)v[8],  (int)v[9],  (int)v[10], (int)v[11],
        };
        XFree(data);
        return out->left || out->right || out->top || out->bottom;
    }
    if (data) { XFree(data); data = NULL; }

    /* Fall back to the older _NET_WM_STRUT (no start/end coordinates). */
    if (XGetWindowProperty(app->dpy, w, app->atom_net_wm_strut,
                           0, 4, False, XA_CARDINAL,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &data) == Success
        && data && actual_format == 32 && nitems >= 4) {
        unsigned long *v = (unsigned long *)data;
        int sw = DisplayWidth(app->dpy, app->screen);
        int sh = DisplayHeight(app->dpy, app->screen);
        *out = (Strut){
            (int)v[0], (int)v[1], (int)v[2], (int)v[3],
            0, sh - 1, 0, sh - 1,
            0, sw - 1, 0, sw - 1,
        };
        XFree(data);
        return out->left || out->right || out->top || out->bottom;
    }
    if (data) XFree(data);
    return false;
}

/* Clip a single monitor rect so it does not overlap the reserved area
 * described by one dock window's strut. */
static void apply_strut_to_monitor(Rect *mon, const Strut *s, int sw, int sh) {
    int mx2 = mon->x + mon->width;
    int my2 = mon->y + mon->height;

    /* Left strut: reserved columns x=[0, left-1], rows=[left_start_y, left_end_y] */
    if (s->left > 0 && mon->x < s->left
        && my2 > s->left_start_y && mon->y <= s->left_end_y) {
        int new_x = s->left;
        mon->width -= (new_x - mon->x);
        mon->x = new_x;
        if (mon->width < 0) mon->width = 0;
    }

    /* Right strut: reserved columns x=[sw-right, sw-1], rows=[right_start_y, right_end_y] */
    if (s->right > 0 && mx2 > sw - s->right
        && my2 > s->right_start_y && mon->y <= s->right_end_y) {
        mon->width = (sw - s->right) - mon->x;
        if (mon->width < 0) mon->width = 0;
    }

    /* Top strut: reserved rows y=[0, top-1], cols=[top_start_x, top_end_x] */
    if (s->top > 0 && mon->y < s->top
        && mx2 > s->top_start_x && mon->x <= s->top_end_x) {
        int new_y = s->top;
        mon->height -= (new_y - mon->y);
        mon->y = new_y;
        if (mon->height < 0) mon->height = 0;
    }

    /* Bottom strut: reserved rows y=[sh-bottom, sh-1], cols=[bottom_start_x, bottom_end_x] */
    if (s->bottom > 0 && my2 > sh - s->bottom
        && mx2 > s->bottom_start_x && mon->x <= s->bottom_end_x) {
        mon->height = (sh - s->bottom) - mon->y;
        if (mon->height < 0) mon->height = 0;
    }
}

/* Walk all root-window children and clip every monitor rect to exclude the
 * reserved strut areas of any window that declares one.  We do not filter by
 * _NET_WM_WINDOW_TYPE_DOCK because Fluxbox's toolbar does not always set that
 * atom — but it always sets _NET_WM_STRUT / _NET_WM_STRUT_PARTIAL.  Windows
 * without a strut property are skipped by get_window_strut() returning false. */
static void apply_dock_struts(App *app, Rect mons[], int nmon) {
    int sw = DisplayWidth(app->dpy, app->screen);
    int sh = DisplayHeight(app->dpy, app->screen);

    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(app->dpy, app->root, &root_ret, &parent_ret, &children, &nchildren))
        return;

    for (unsigned int i = 0; i < nchildren; i++) {
        Strut strut;
        if (!get_window_strut(app, children[i], &strut))
            continue;

        for (int m = 0; m < nmon; m++)
            apply_strut_to_monitor(&mons[m], &strut, sw, sh);
    }

    if (children) XFree(children);
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
    if (r.width < 1) r.width = 1;
    if (r.height < 1) r.height = 1;

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

static Rect zone_rect_for_monitor(const Rect *monitor, const Zone *z, int global_gap) {
    Rect base = {
        .x = monitor->x + global_gap,
        .y = monitor->y + global_gap,
        .width = monitor->width - (2 * global_gap),
        .height = monitor->height - (2 * global_gap),
        .valid = true,
    };

    Rect r = {
        .x = base.x + (base.width * z->x_pct) / 100,
        .y = base.y + (base.height * z->y_pct) / 100,
        .width = (base.width * z->w_pct) / 100,
        .height = (base.height * z->h_pct) / 100,
        .valid = true,
    };

    r.x += z->gap;
    r.y += z->gap;
    r.width -= z->gap * 2;
    r.height -= z->gap * 2;
    if (r.width < 1) r.width = 1;
    if (r.height < 1) r.height = 1;
    return r;
}

static void layout_zone(App *app, ZoneBucket *b, ZoneLayout layout, int gap) {
    int n = b->count;
    if (n <= 0) return;

    Rect a = b->area;
    if (layout == ZONE_COLS) {
        int usable = a.width - ((n - 1) * gap);
        if (usable < n) usable = n;
        int x = a.x;
        int base = usable / n;
        int rem = usable % n;
        for (int i = 0; i < n; i++) {
            int w = base + (i < rem ? 1 : 0);
            apply_rect(app, b->windows[i], (Rect){x, a.y, w, a.height, true});
            x += w + gap;
        }
        return;
    }

    if (layout == ZONE_GRID) {
        int cols = 1;
        while (cols * cols < n) cols++;
        int rows = (n + cols - 1) / cols;

        int usable_w = a.width - ((cols - 1) * gap);
        int usable_h = a.height - ((rows - 1) * gap);
        if (usable_w < cols) usable_w = cols;
        if (usable_h < rows) usable_h = rows;

        int cw = usable_w / cols;
        int ch = usable_h / rows;
        int idx = 0;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols && idx < n; c++) {
                int x = a.x + c * (cw + gap);
                int y = a.y + r * (ch + gap);
                int w = (c == cols - 1) ? (a.x + a.width - x) : cw;
                int h = (r == rows - 1) ? (a.y + a.height - y) : ch;
                apply_rect(app, b->windows[idx++], (Rect){x, y, w, h, true});
            }
        }
        return;
    }

    int usable = a.height - ((n - 1) * gap);
    if (usable < n) usable = n;
    int y = a.y;
    int base = usable / n;
    int rem = usable % n;
    for (int i = 0; i < n; i++) {
        int h = base + (i < rem ? 1 : 0);
        apply_rect(app, b->windows[i], (Rect){a.x, y, a.width, h, true});
        y += h + gap;
    }
}

static int monitor_index_for_window_center(App *app, const Rect mons[], int nmon, Window w) {
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(app->dpy, w, &attrs)) return 0;

    Window child;
    int rx = 0;
    int ry = 0;
    XTranslateCoordinates(app->dpy,
                          w,
                          app->root,
                          attrs.width / 2,
                          attrs.height / 2,
                          &rx,
                          &ry,
                          &child);

    for (int i = 0; i < nmon; i++) {
        if (rx >= mons[i].x && rx < mons[i].x + mons[i].width && ry >= mons[i].y && ry < mons[i].y + mons[i].height) {
            return i;
        }
    }
    return 0;
}

static void tile_all_windows(App *app) {
    Window wins[MAX_MANAGED];
    int count = load_client_list(app, wins);
    if (count <= 0) return;

    Rect wa = get_workarea(app);
    Rect mons[MAX_MONITORS] = {0};
    int nmon = get_visible_monitors(app, wa, mons);
    apply_dock_struts(app, mons, nmon);

    /* Apply explicit band reservations from config.  These act as a floor:
     * if strut detection already reserved more space on a side, no change;
     * if strut detection missed the toolbar (common with some Fluxbox builds),
     * the configured band takes over.  top_band/bottom_band/left_band/right_band
     * are in pixels from the respective screen edge. */
    {
        const Config *c = &app->config;
        if (c->band_top || c->band_bottom || c->band_left || c->band_right) {
            int sw = DisplayWidth(app->dpy, app->screen);
            int sh = DisplayHeight(app->dpy, app->screen);
            Strut band = {
                c->band_left, c->band_right, c->band_top, c->band_bottom,
                0, sh - 1,  /* left  start_y / end_y */
                0, sh - 1,  /* right start_y / end_y */
                0, sw - 1,  /* top   start_x / end_x */
                0, sw - 1,  /* bottom start_x / end_x */
            };
            for (int m = 0; m < nmon; m++)
                apply_strut_to_monitor(&mons[m], &band, sw, sh);
        }
    }

    for (int m = 0; m < nmon; m++) {
        ZoneBucket buckets[MAX_ZONES] = {0};
        for (int z = 0; z < app->config.zone_count; z++) {
            buckets[z].area = zone_rect_for_monitor(&mons[m], &app->config.zones[z], app->config.gap);
        }

        for (int i = 0; i < count; i++) {
            int mon = monitor_index_for_window_center(app, mons, nmon, wins[i]);
            if (mon != m) continue;

            int chosen = -1;
            for (int z = 0; z < app->config.zone_count; z++) {
                int maxw = app->config.zones[z].max_windows;
                if (maxw == 0 || buckets[z].count < maxw) {
                    if (chosen < 0 || buckets[z].count < buckets[chosen].count) chosen = z;
                }
            }
            if (chosen < 0) chosen = app->config.zone_count - 1;

            if (buckets[chosen].count < MAX_MANAGED) {
                buckets[chosen].windows[buckets[chosen].count++] = wins[i];
            }
        }

        for (int z = 0; z < app->config.zone_count; z++) {
            if (buckets[z].count == 0) continue;
            layout_zone(app, &buckets[z], app->config.zones[z].layout, app->config.zones[z].gap);
        }
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
    app.atom_net_wm_strut = XInternAtom(app.dpy, "_NET_WM_STRUT", False);
    app.atom_net_wm_strut_partial = XInternAtom(app.dpy, "_NET_WM_STRUT_PARTIAL", False);

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
