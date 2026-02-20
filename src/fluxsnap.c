#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <sys/param.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <unistd.h>

#define DEFAULT_EDGE_THRESHOLD 56
#define DEFAULT_TOP_BAND 84
#define DEFAULT_GAP 8

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
    unsigned int width;
    unsigned int height;
    bool valid;
} Rect;

typedef struct {
    Display *dpy;
    int screen;
    Window root;
    Window preview;
    Config config;
    Window target;
    bool dragging;
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

static void init_preview(App *app) {
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = 0x3030ff;
    attrs.border_pixel = 0xffffff;

    app->preview = XCreateWindow(
        app->dpy,
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
        &attrs);
    XSelectInput(app->dpy, app->preview, ExposureMask);
}

static Window top_level_window(Display *dpy, Window w, Window root) {
    Window parent = w;
    Window next_parent;
    Window *children = NULL;
    unsigned int nchildren;

    while (true) {
        if (!XQueryTree(dpy, parent, &root, &next_parent, &children, &nchildren)) {
            break;
        }
        if (children) XFree(children);
        if (next_parent == root || next_parent == 0) {
            return parent;
        }
        parent = next_parent;
    }

    return w;
}

static void normalize_rect(Rect *r) {
    if (!r->valid) return;
    if ((int)r->width < 1) r->width = 1;
    if ((int)r->height < 1) r->height = 1;
}

static Rect compute_snap_rect(App *app, int pointer_x, int pointer_y) {
    Rect r = {0};
    int sw = DisplayWidth(app->dpy, app->screen);
    int sh = DisplayHeight(app->dpy, app->screen);
    int t = app->config.edge_threshold;
    int top = app->config.top_band;
    int gap = app->config.gap;

    bool at_left = pointer_x <= t;
    bool at_right = pointer_x >= (sw - t);
    bool at_top = pointer_y <= t;
    bool at_bottom = pointer_y >= (sh - t);
    bool in_top_band = pointer_y <= top;

    if (at_left && at_top) {
        r = (Rect){gap, gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh / 2 - (gap * 2)), true};
    } else if (at_right && at_top) {
        r = (Rect){sw / 2 + gap, gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh / 2 - (gap * 2)), true};
    } else if (at_left && at_bottom) {
        r = (Rect){gap, sh / 2 + gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh / 2 - (gap * 2)), true};
    } else if (at_right && at_bottom) {
        r = (Rect){sw / 2 + gap, sh / 2 + gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh / 2 - (gap * 2)), true};
    } else if (at_left) {
        r = (Rect){gap, gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh - (gap * 2)), true};
    } else if (at_right) {
        r = (Rect){sw / 2 + gap, gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh - (gap * 2)), true};
    } else if (at_top || in_top_band) {
        if (pointer_x < sw / 3) {
            r = (Rect){gap, gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh - (gap * 2)), true};
        } else if (pointer_x > (sw * 2) / 3) {
            r = (Rect){sw / 2 + gap, gap, (unsigned int)(sw / 2 - (gap * 2)), (unsigned int)(sh - (gap * 2)), true};
        } else {
            r = (Rect){gap, gap, (unsigned int)(sw - (gap * 2)), (unsigned int)(sh - (gap * 2)), true};
        }
    }

    normalize_rect(&r);
    return r;
}

static bool rect_equal(const Rect *a, const Rect *b) {
    return a->valid == b->valid && a->x == b->x && a->y == b->y &&
           a->width == b->width && a->height == b->height;
}

static void show_preview(App *app, const Rect *r) {
    if (!r->valid) {
        XUnmapWindow(app->dpy, app->preview);
        return;
    }

    XMoveResizeWindow(app->dpy, app->preview, r->x, r->y, r->width, r->height);
    XMapRaised(app->dpy, app->preview);
}

static void apply_snap(App *app, const Rect *r) {
    if (!app->target || !r->valid) return;
    XMoveResizeWindow(app->dpy, app->target, r->x, r->y, r->width, r->height);
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

    init_preview(&app);
    grab_with_lock_variants(&app, app.config.button, app.config.modifier);
    XSync(app.dpy, False);

    for (;;) {
        XEvent ev;
        XNextEvent(app.dpy, &ev);

        if (ev.type == ButtonPress) {
            XButtonEvent *bev = &ev.xbutton;
            if (bev->subwindow == None) continue;
            app.target = top_level_window(app.dpy, bev->subwindow, app.root);
            app.dragging = true;
            app.current_rect = (Rect){0};
            XRaiseWindow(app.dpy, app.target);
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
            XUnmapWindow(app.dpy, app.preview);
            app.dragging = false;
            app.target = None;
            app.current_rect = (Rect){0};
            XSync(app.dpy, False);
        }
    }

    return 0;
}
