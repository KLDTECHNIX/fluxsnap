#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/shape.h>
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

#define DEFAULT_EDGE_THRESHOLD 56
#define DEFAULT_TOP_BAND 84
#define DEFAULT_GAP 8
#define OVERLAY_OPACITY 0xC0000000UL
#define PREVIEW_OPACITY 0xA0000000UL

typedef struct {
    unsigned int modifier;
    unsigned int button;
    int edge_threshold;
    int top_band;
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
    Display *dpy;
    int screen;
    Window root;
    Window overlay;
    Window preview;
    Atom atom_wm_state;
    Atom atom_net_wm_state;
    Atom atom_net_wm_state_max_horz;
    Atom atom_net_wm_state_max_vert;
    Atom atom_net_moveresize_window;
    Atom atom_net_wm_window_opacity;
    Atom atom_net_workarea;
    Atom atom_net_current_desktop;
    Config config;
    Window target;
    bool dragging;
    bool modifier_down;
    Rect current_rect;
} App;

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
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
    cfg->button = Button1;
    cfg->edge_threshold = DEFAULT_EDGE_THRESHOLD;
    cfg->top_band = DEFAULT_TOP_BAND;
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
        } else if (strcasecmp(key, "mouse_button") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 1 && v <= 5) cfg->button = (unsigned int)v;
        } else if (strcasecmp(key, "edge_threshold") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 1 && v <= 500) cfg->edge_threshold = (int)v;
        } else if (strcasecmp(key, "top_band") == 0) {
            long v = strtol(value, NULL, 10);
            if (v >= 1 && v <= 500) cfg->top_band = (int)v;
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

static void set_window_opacity(App *app, Window w, unsigned long opacity) {
    if (app->atom_net_wm_window_opacity == None) return;
    XChangeProperty(app->dpy,
                    w,
                    app->atom_net_wm_window_opacity,
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *)&opacity,
                    1);
}

static void make_input_passthrough(App *app, Window w) {
    int shape_ev;
    int shape_err;
    if (!XShapeQueryExtension(app->dpy, &shape_ev, &shape_err)) return;
    XShapeCombineRectangles(app->dpy, w, ShapeInput, 0, 0, NULL, 0, ShapeSet, 0);
}

static void init_visuals(App *app) {
    XSetWindowAttributes dim_attrs;
    dim_attrs.override_redirect = True;
    dim_attrs.background_pixel = BlackPixel(app->dpy, app->screen);

    int sw = DisplayWidth(app->dpy, app->screen);
    int sh = DisplayHeight(app->dpy, app->screen);

    app->overlay = XCreateWindow(app->dpy,
                                 app->root,
                                 0,
                                 0,
                                 (unsigned int)sw,
                                 (unsigned int)sh,
                                 0,
                                 CopyFromParent,
                                 InputOutput,
                                 CopyFromParent,
                                 CWOverrideRedirect | CWBackPixel,
                                 &dim_attrs);

    XSetWindowAttributes preview_attrs;
    preview_attrs.override_redirect = True;
    preview_attrs.background_pixel = 0xf3f3f3;
    preview_attrs.border_pixel = 0xffffff;

    app->preview = XCreateWindow(app->dpy,
                                 app->root,
                                 0,
                                 0,
                                 1,
                                 1,
                                 2,
                                 CopyFromParent,
                                 InputOutput,
                                 CopyFromParent,
                                 CWOverrideRedirect | CWBackPixel | CWBorderPixel,
                                 &preview_attrs);

    set_window_opacity(app, app->overlay, OVERLAY_OPACITY);
    set_window_opacity(app, app->preview, PREVIEW_OPACITY);
    make_input_passthrough(app, app->overlay);
    make_input_passthrough(app, app->preview);
}

static bool has_property(App *app, Window w, Atom property) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *prop = NULL;

    int rc = XGetWindowProperty(app->dpy,
                                w,
                                property,
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

static Window find_client_child(App *app, Window w, int depth) {
    if (depth > 6) return None;
    if (has_property(app, w, app->atom_wm_state)) return w;

    Window root_ret;
    Window parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;
    if (!XQueryTree(app->dpy, w, &root_ret, &parent_ret, &children, &nchildren)) {
        return None;
    }

    Window result = None;
    for (unsigned int i = 0; i < nchildren; i++) {
        result = find_client_child(app, children[i], depth + 1);
        if (result != None) break;
    }
    if (children) XFree(children);
    return result;
}

static Window resolve_client_window(App *app, Window w) {
    Window candidate = w;
    Window root_ret;
    Window parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    while (candidate != None) {
        if (has_property(app, candidate, app->atom_wm_state)) return candidate;

        Window child_candidate = find_client_child(app, candidate, 0);
        if (child_candidate != None) return child_candidate;

        if (!XQueryTree(app->dpy, candidate, &root_ret, &parent_ret, &children, &nchildren)) {
            break;
        }
        if (children) XFree(children);
        if (parent_ret == root_ret || parent_ret == None) break;
        candidate = parent_ret;
    }

    return w;
}

static bool root_cardinal(App *app, Atom property, unsigned long **out, unsigned long *count) {
    if (property == None) return false;

    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
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

    if (actual_type != XA_CARDINAL || actual_format != 32 || !prop || nitems == 0) {
        if (prop) XFree(prop);
        return false;
    }

    *out = (unsigned long *)prop;
    *count = nitems;
    return true;
}

static Rect get_workarea(App *app) {
    Rect wa = {
        .x = 0,
        .y = 0,
        .width = DisplayWidth(app->dpy, app->screen),
        .height = DisplayHeight(app->dpy, app->screen),
        .valid = true,
    };

    unsigned long *workareas = NULL;
    unsigned long wa_count = 0;
    if (!root_cardinal(app, app->atom_net_workarea, &workareas, &wa_count)) {
        return wa;
    }

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
        wa.x = 0;
        wa.y = 0;
        wa.width = DisplayWidth(app->dpy, app->screen);
        wa.height = DisplayHeight(app->dpy, app->screen);
    }

    return wa;
}

static void normalize_rect(Rect *r) {
    if (!r->valid) return;
    if (r->width < 1) r->width = 1;
    if (r->height < 1) r->height = 1;
}

static Rect rect_full(const Rect *wa, int gap) {
    Rect r = {
        .x = wa->x + gap,
        .y = wa->y + gap,
        .width = wa->width - (gap * 2),
        .height = wa->height - (gap * 2),
        .valid = true,
    };
    normalize_rect(&r);
    return r;
}

static void split_horizontal(const Rect *wa, int gap, Rect *left, Rect *right) {
    int inner = gap;
    int x = wa->x + gap;
    int y = wa->y + gap;
    int h = wa->height - (gap * 2);
    int w = wa->width - (gap * 2) - inner;
    if (w < 2) w = 2;

    int left_w = w / 2;
    int right_w = w - left_w;

    *left = (Rect){x, y, left_w, h, true};
    *right = (Rect){x + left_w + inner, y, right_w, h, true};
    normalize_rect(left);
    normalize_rect(right);
}

static void split_vertical(const Rect *base, int gap, Rect *top, Rect *bottom) {
    int inner = gap;
    int h = base->height - inner;
    if (h < 2) h = 2;

    int top_h = h / 2;
    int bottom_h = h - top_h;

    *top = (Rect){base->x, base->y, base->width, top_h, true};
    *bottom = (Rect){base->x, base->y + top_h + inner, base->width, bottom_h, true};
    normalize_rect(top);
    normalize_rect(bottom);
}

static Rect compute_snap_rect(App *app, int pointer_x, int pointer_y) {
    Rect r = {0};
    Rect wa = get_workarea(app);
    int t = app->config.edge_threshold;
    int top_band = app->config.top_band;
    int gap = app->config.gap;

    bool at_left = pointer_x <= (wa.x + t);
    bool at_right = pointer_x >= (wa.x + wa.width - t);
    bool at_top = pointer_y <= (wa.y + t);
    bool at_bottom = pointer_y >= (wa.y + wa.height - t);
    bool in_top_band = pointer_y <= (wa.y + top_band);

    Rect left_half, right_half;
    split_horizontal(&wa, gap, &left_half, &right_half);

    Rect left_top, left_bottom, right_top, right_bottom;
    split_vertical(&left_half, gap, &left_top, &left_bottom);
    split_vertical(&right_half, gap, &right_top, &right_bottom);

    if (at_left && at_top) {
        r = left_top;
    } else if (at_right && at_top) {
        r = right_top;
    } else if (at_left && at_bottom) {
        r = left_bottom;
    } else if (at_right && at_bottom) {
        r = right_bottom;
    } else if (at_left) {
        r = left_half;
    } else if (at_right) {
        r = right_half;
    } else if (at_top || in_top_band) {
        if (pointer_x < (wa.x + wa.width / 3)) {
            r = left_half;
        } else if (pointer_x > (wa.x + (wa.width * 2) / 3)) {
            r = right_half;
        } else {
            r = rect_full(&wa, gap);
        }
    }

    normalize_rect(&r);
    return r;
}

static bool rect_equal(const Rect *a, const Rect *b) {
    return a->valid == b->valid && a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

static void set_overlay_visible(App *app, bool visible) {
    if (visible) {
        int sw = DisplayWidth(app->dpy, app->screen);
        int sh = DisplayHeight(app->dpy, app->screen);
        XMoveResizeWindow(app->dpy, app->overlay, 0, 0, (unsigned int)sw, (unsigned int)sh);
        XMapRaised(app->dpy, app->overlay);
        if (app->dragging && app->current_rect.valid) {
            XMapRaised(app->dpy, app->preview);
        }
    } else {
        XUnmapWindow(app->dpy, app->overlay);
        XUnmapWindow(app->dpy, app->preview);
    }
}

static void show_preview(App *app, const Rect *r) {
    if (!r->valid || !app->modifier_down) {
        XUnmapWindow(app->dpy, app->preview);
        return;
    }

    XMoveResizeWindow(app->dpy, app->preview, r->x, r->y, (unsigned int)r->width, (unsigned int)r->height);
    XMapRaised(app->dpy, app->preview);
}

static void clear_maximized_state(App *app, Window w) {
    if (app->atom_net_wm_state == None || app->atom_net_wm_state_max_horz == None || app->atom_net_wm_state_max_vert == None) {
        return;
    }

    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = app->atom_net_wm_state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 0;
    ev.xclient.data.l[1] = app->atom_net_wm_state_max_horz;
    ev.xclient.data.l[2] = app->atom_net_wm_state_max_vert;
    ev.xclient.data.l[3] = 2;

    XSendEvent(app->dpy,
               app->root,
               False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &ev);
}

static bool apply_snap_via_ewmh(App *app, Window w, const Rect *r) {
    if (app->atom_net_moveresize_window == None) return false;

    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.message_type = app->atom_net_moveresize_window;
    ev.xclient.window = w;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (1L << 8) | (1L << 9) | (1L << 10) | (1L << 11);
    ev.xclient.data.l[1] = r->x;
    ev.xclient.data.l[2] = r->y;
    ev.xclient.data.l[3] = r->width;
    ev.xclient.data.l[4] = r->height;

    Status sent = XSendEvent(app->dpy,
                             app->root,
                             False,
                             SubstructureRedirectMask | SubstructureNotifyMask,
                             &ev);
    return sent != 0;
}

static void apply_snap(App *app, const Rect *r) {
    if (!app->target || !r->valid) return;

    clear_maximized_state(app, app->target);
    if (!apply_snap_via_ewmh(app, app->target, r)) {
        XMoveResizeWindow(app->dpy,
                          app->target,
                          r->x,
                          r->y,
                          (unsigned int)r->width,
                          (unsigned int)r->height);
    }
    XRaiseWindow(app->dpy, app->target);
}

static void grab_with_lock_variants(App *app, unsigned int button, unsigned int modifier) {
    const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    for (size_t i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
        XGrabButton(app->dpy,
                    button,
                    modifier | masks[i],
                    app->root,
                    True,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync,
                    GrabModeAsync,
                    None,
                    None);
    }
}

static size_t modifier_keysyms(unsigned int modmask, KeySym out[8]) {
    if (modmask == ControlMask) {
        out[0] = XK_Control_L;
        out[1] = XK_Control_R;
        return 2;
    }
    if (modmask == ShiftMask) {
        out[0] = XK_Shift_L;
        out[1] = XK_Shift_R;
        return 2;
    }
    if (modmask == Mod1Mask) {
        out[0] = XK_Alt_L;
        out[1] = XK_Alt_R;
        out[2] = XK_Meta_L;
        out[3] = XK_Meta_R;
        return 4;
    }

    out[0] = XK_Super_L;
    out[1] = XK_Super_R;
    out[2] = XK_Hyper_L;
    out[3] = XK_Hyper_R;
    return 4;
}

static void grab_modifier_keys(App *app, unsigned int modmask) {
    const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    KeySym syms[8];
    size_t nsyms = modifier_keysyms(modmask, syms);

    for (size_t k = 0; k < nsyms; k++) {
        KeyCode code = XKeysymToKeycode(app->dpy, syms[k]);
        if (code == 0) continue;
        for (size_t i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
            XGrabKey(app->dpy,
                     (int)code,
                     masks[i],
                     app->root,
                     True,
                     GrabModeAsync,
                     GrabModeAsync);
        }
    }
}

static bool is_modifier_key(App *app, KeyCode code) {
    KeySym sym = XkbKeycodeToKeysym(app->dpy, code, 0, 0);
    if (app->config.modifier == ControlMask) {
        return sym == XK_Control_L || sym == XK_Control_R;
    }
    if (app->config.modifier == ShiftMask) {
        return sym == XK_Shift_L || sym == XK_Shift_R;
    }
    if (app->config.modifier == Mod1Mask) {
        return sym == XK_Alt_L || sym == XK_Alt_R || sym == XK_Meta_L || sym == XK_Meta_R;
    }

    return sym == XK_Super_L || sym == XK_Super_R || sym == XK_Hyper_L || sym == XK_Hyper_R;
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
    app.atom_net_wm_state = XInternAtom(app.dpy, "_NET_WM_STATE", False);
    app.atom_net_wm_state_max_horz = XInternAtom(app.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    app.atom_net_wm_state_max_vert = XInternAtom(app.dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    app.atom_net_moveresize_window = XInternAtom(app.dpy, "_NET_MOVERESIZE_WINDOW", False);
    app.atom_net_wm_window_opacity = XInternAtom(app.dpy, "_NET_WM_WINDOW_OPACITY", False);
    app.atom_net_workarea = XInternAtom(app.dpy, "_NET_WORKAREA", False);
    app.atom_net_current_desktop = XInternAtom(app.dpy, "_NET_CURRENT_DESKTOP", False);

    init_visuals(&app);
    grab_with_lock_variants(&app, app.config.button, app.config.modifier);
    grab_modifier_keys(&app, app.config.modifier);
    XSync(app.dpy, False);

    for (;;) {
        XEvent ev;
        XNextEvent(app.dpy, &ev);

        if (ev.type == KeyPress) {
            XKeyEvent *kev = &ev.xkey;
            if (is_modifier_key(&app, kev->keycode)) {
                app.modifier_down = true;
                set_overlay_visible(&app, true);
                XSync(app.dpy, False);
            }
        } else if (ev.type == KeyRelease) {
            XKeyEvent *kev = &ev.xkey;
            if (is_modifier_key(&app, kev->keycode)) {
                app.modifier_down = false;
                if (!app.dragging) set_overlay_visible(&app, false);
                XSync(app.dpy, False);
            }
        } else if (ev.type == ButtonPress) {
            XButtonEvent *bev = &ev.xbutton;
            if (bev->subwindow == None) continue;
            app.target = resolve_client_window(&app, bev->subwindow);
            app.dragging = true;
            app.modifier_down = true;
            app.current_rect = (Rect){0};
            set_overlay_visible(&app, true);
            XRaiseWindow(app.dpy, app.target);
            XSync(app.dpy, False);
        } else if (ev.type == MotionNotify && app.dragging) {
            XMotionEvent *mev = &ev.xmotion;
            Rect next = compute_snap_rect(&app, mev->x_root, mev->y_root);
            if (!rect_equal(&next, &app.current_rect)) {
                app.current_rect = next;
                show_preview(&app, &next);
                XSync(app.dpy, False);
            }
        } else if (ev.type == ButtonRelease && app.dragging) {
            apply_snap(&app, &app.current_rect);
            app.dragging = false;
            app.target = None;
            app.current_rect = (Rect){0};
            if (!app.modifier_down) {
                set_overlay_visible(&app, false);
            } else {
                XUnmapWindow(app.dpy, app.preview);
            }
            XSync(app.dpy, False);
        }
    }

    return 0;
}
