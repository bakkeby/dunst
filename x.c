/* copyright 2013 Sascha Kruse and contributors (see LICENSE for licensing information) */
#include <math.h>
#include <sys/time.h>
#include <ctype.h>
#include <assert.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <pango/pangocairo.h>
#include <cairo-xlib.h>

#include "x.h"
#include "utils.h"
#include "dunst.h"
#include "settings.h"
#include "notification.h"

#define WIDTH 400
#define HEIGHT 400

xctx_t xctx;
bool dunst_grab_errored = false;

typedef struct _cairo_ctx {
        cairo_status_t status;
        cairo_surface_t *surface;
        PangoFontDescription *desc;
} cairo_ctx_t;

cairo_ctx_t cairo_ctx;


static void x_shortcut_setup_error_handler(void);
static int x_shortcut_tear_down_error_handler(void);
static void x_win_move(int width, int height);

void x_cairo_setup(void)
{
        cairo_ctx.surface = cairo_xlib_surface_create(xctx.dpy,
                        xctx.win, DefaultVisual(xctx.dpy, 0), WIDTH, HEIGHT);

        cairo_ctx.desc = pango_font_description_from_string(settings.font);
}

void r_setup_pango_layout(PangoLayout *layout, int width)
{
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, width * PANGO_SCALE);
        pango_layout_set_font_description(layout, cairo_ctx.desc);
        pango_layout_set_spacing(layout, settings.line_height * PANGO_SCALE);

        PangoAlignment align;
        switch (settings.align) {
                case left:
                        align = PANGO_ALIGN_LEFT;
                        break;
                case center:
                        align = PANGO_ALIGN_CENTER;
                        break;
                case right:
                        align = PANGO_ALIGN_RIGHT;
                        break;
        }
        pango_layout_set_alignment(layout, align);

}

PangoLayout *r_create_layout_from_notification(cairo_t *c, notification *n)
{
        PangoLayout *layout = pango_cairo_create_layout(c);

        notification_update_text_to_render(n);

        int width = -1;
        if (xctx.geometry.w > 0) {
                width = xctx.geometry.w - 2 * settings.h_padding;
                width -= 2 * settings.frame_width;
        }
        r_setup_pango_layout(layout, width);

        pango_layout_set_text(layout, n->text_to_render, -1);

        pango_layout_get_pixel_size(layout, NULL, &(n->displayed_height));
        n->displayed_height += 2 * settings.padding;

        return layout;
}

GSList *r_create_layouts(cairo_t *c)
{
        GSList *layouts = NULL;

        for (GList *iter = g_queue_peek_head_link(displayed);
                        iter; iter = iter->next)
        {
                notification *n = iter->data;
                layouts = g_slist_append(layouts,
                                r_create_layout_from_notification(c, n));
        }

        return layouts;
}

void r_free_layouts(GSList *layouts)
{
        g_slist_free_full(layouts, g_object_unref);
}

void x_win_draw(void)
{
        cairo_t *c;
        c = cairo_create(cairo_ctx.surface);

        GSList *layouts = r_create_layouts(c);
        int height = 0;
        int text_width = 0;

        for (GSList *iter = layouts; iter; iter = iter->next) {
                PangoLayout *l = iter->data;
                int w,h;
                pango_layout_get_pixel_size(l, &w, &h);
                height += h;
                text_width = MAX(w, text_width);
        }

        int width;
        if (xctx.geometry.w > 0)
                width = xctx.geometry.w;
        else {
                width = text_width + 2 * settings.h_padding;
                width += 2 * settings.frame_width;
        }

        height += (g_slist_length(layouts) - 1) * settings.separator_height;
        height += g_slist_length(layouts) * settings.padding * 2;

        XResizeWindow(xctx.dpy, xctx.win, width, height);

        /* FIXME frame color */
        cairo_set_source_rgb(c, 0.0, 0.0, 0.8);
        cairo_rectangle(c, 0.0, 0.0, width, height);
        cairo_fill(c);

        /* FIXME text color */
        cairo_set_source_rgb(c, 0.8, 0.8, 0.8);
        cairo_move_to(c, 0, 0);

        double y = 0;

        bool first = true;
        for (GSList *iter = layouts; iter; iter = iter->next) {
                PangoLayout *l = iter->data;

                int h;
                pango_layout_get_pixel_size(l, NULL, &h);

                int bg_x = 0;
                int bg_y = y;
                int bg_width = width;
                int bg_height = (2 * settings.h_padding) + h;

                /* adding frame */
                bg_x += settings.frame_width;
                if (first) {
                        bg_y += settings.frame_width;
                        bg_height -= settings.frame_width;
                }
                bg_width -= 2 * settings.frame_width;
                if (!iter->next)
                        bg_height -= settings.frame_width;

                /* FIXME background color */
                cairo_set_source_rgb(c, 0.2, 0.2, 0.2);
                cairo_rectangle(c, bg_x, bg_y, bg_width, bg_height);
                cairo_fill(c);

                y += settings.padding;
                cairo_move_to(c, settings.h_padding, y);
                /* FIXME text color */
                cairo_set_source_rgb(c, 0.8, 0.8, 0.8);
                pango_cairo_update_layout(c, l);
                pango_cairo_show_layout(c, l);
                y += h + settings.padding;
                if (settings.separator_height > 0 && iter->next) {
                        cairo_move_to(c, 0, y + settings.separator_height / 2);
                        /* FIXME sep_color */
                        cairo_set_source_rgb(c, 0.8, 0.0, 0.0);
                        cairo_set_line_width(c, settings.separator_height);
                        cairo_line_to(c, width, y);
                        y += settings.separator_height;
                        cairo_stroke(c);
                }
                cairo_move_to(c, settings.h_padding, y);
                first = false;
        }

        cairo_show_page(c);

        x_win_move(width, height);

        cairo_destroy(c);
        r_free_layouts(layouts);

}

static void x_win_move(int width, int height)
{

        int x, y;
        screen_info scr;
        x_screen_info(&scr);
        /* calculate window position */
        if (xctx.geometry.mask & XNegative) {
                x = (scr.dim.x + (scr.dim.w - width)) + xctx.geometry.x;
        } else {
                x = scr.dim.x + xctx.geometry.x;
        }

        if (xctx.geometry.mask & YNegative) {
                y = scr.dim.y + (scr.dim.h + xctx.geometry.y) - height;
        } else {
                y = scr.dim.y + xctx.geometry.y;
        }

        /* move and map window */
        if (x != xctx.window_dim.x || y != xctx.window_dim.y
            || width != xctx.window_dim.w || height != xctx.window_dim.h) {

                XMoveWindow(xctx.dpy, xctx.win, x, y);

                xctx.window_dim.x = x;
                xctx.window_dim.y = y;
                xctx.window_dim.h = height;
                xctx.window_dim.w = width;
        }
}


void setopacity(Window win, unsigned long opacity)
{
        Atom _NET_WM_WINDOW_OPACITY =
            XInternAtom(xctx.dpy, "_NET_WM_WINDOW_OPACITY", false);
        XChangeProperty(xctx.dpy, win, _NET_WM_WINDOW_OPACITY, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&opacity, 1L);
}








        /*
         * Helper function to use glib's mainloop mechanic
         * with Xlib
         */
gboolean x_mainloop_fd_prepare(GSource * source, gint * timeout)
{
        *timeout = -1;
        return false;
}

        /*
         * Helper function to use glib's mainloop mechanic
         * with Xlib
         */
gboolean x_mainloop_fd_check(GSource * source)
{
        return XPending(xctx.dpy) > 0;
}

        /*
         * Main Dispatcher for XEvents
         */
gboolean x_mainloop_fd_dispatch(GSource * source, GSourceFunc callback,
                                gpointer user_data)
{
        XEvent ev;
        while (XPending(xctx.dpy) > 0) {
                XNextEvent(xctx.dpy, &ev);
                switch (ev.type) {
                case Expose:
                        if (ev.xexpose.count == 0 && xctx.visible) {
                        }
                        break;
                case SelectionNotify:
                        if (ev.xselection.property == xctx.utf8)
                                break;
                case VisibilityNotify:
                        if (ev.xvisibility.state != VisibilityUnobscured)
                                XRaiseWindow(xctx.dpy, xctx.win);
                        break;
                case ButtonPress:
                        if (ev.xbutton.window == xctx.win) {
                                x_handle_click(ev);
                        }
                        break;
                case KeyPress:
                        if (settings.close_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.close_ks.sym
                            && settings.close_ks.mask == ev.xkey.state) {
                                if (displayed) {
                                        notification_close
                                            (g_queue_peek_head_link(displayed)->
                                             data, 2);
                                }
                        }
                        if (settings.history_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.history_ks.sym
                            && settings.history_ks.mask == ev.xkey.state) {
                                history_pop();
                        }
                        if (settings.close_all_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.close_all_ks.sym
                            && settings.close_all_ks.mask == ev.xkey.state) {
                                move_all_to_history();
                        }
                        if (settings.context_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.context_ks.sym
                            && settings.context_ks.mask == ev.xkey.state) {
                                context_menu();
                        }
                        break;
                }
        }
        return true;
}

        /*
         * Check whether the user is currently idle.
         */
bool x_is_idle(void)
{
        XScreenSaverQueryInfo(xctx.dpy, DefaultRootWindow(xctx.dpy),
                              xctx.screensaver_info);
        if (settings.idle_threshold == 0) {
                return false;
        }
        return xctx.screensaver_info->idle / 1000 > settings.idle_threshold;
}

/* TODO move to x_mainloop_* */
        /*
         * Handle incoming mouse click events
         */
void x_handle_click(XEvent ev)
{
        if (ev.xbutton.button == Button3) {
                move_all_to_history();

                return;
        }

        if (ev.xbutton.button == Button1) {
                int y = settings.separator_height;
                notification *n = NULL;
                int first = true;
                for (GList * iter = g_queue_peek_head_link(displayed); iter;
                     iter = iter->next) {
                        n = iter->data;
                        if (ev.xbutton.y > y && ev.xbutton.y < y + n->displayed_height)
                                break;

                        y += n->displayed_height + settings.separator_height;
                        if (first)
                                y += settings.frame_width;
                }
                if (n)
                        notification_close(n, 2);
        }
}

        /*
         * Return the window that currently has
         * the keyboard focus.
         */
Window get_focused_window(void)
{
        Window focused = 0;
        Atom type;
        int format;
        unsigned long nitems, bytes_after;
        unsigned char *prop_return = NULL;
        Window root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));
        Atom netactivewindow =
            XInternAtom(xctx.dpy, "_NET_ACTIVE_WINDOW", false);

        XGetWindowProperty(xctx.dpy, root, netactivewindow, 0L,
                           sizeof(Window), false, XA_WINDOW,
                           &type, &format, &nitems, &bytes_after, &prop_return);
        if (prop_return) {
                focused = *(Window *) prop_return;
                XFree(prop_return);
        }

        return focused;
}

#ifdef XINERAMA
        /*
         * Select the screen on which the Window
         * should be displayed.
         */
int select_screen(XineramaScreenInfo * info, int info_len)
{
        if (settings.f_mode == FOLLOW_NONE) {
                return settings.monitor >=
                    0 ? settings.monitor : XDefaultScreen(xctx.dpy);

        } else {
                int x, y;
                assert(settings.f_mode == FOLLOW_MOUSE
                       || settings.f_mode == FOLLOW_KEYBOARD);
                Window root =
                    RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));

                if (settings.f_mode == FOLLOW_MOUSE) {
                        int dummy;
                        unsigned int dummy_ui;
                        Window dummy_win;

                        XQueryPointer(xctx.dpy, root, &dummy_win,
                                      &dummy_win, &x, &y, &dummy,
                                      &dummy, &dummy_ui);
                }

                if (settings.f_mode == FOLLOW_KEYBOARD) {

                        Window focused = get_focused_window();

                        if (focused == 0) {
                                /* something went wrong. Fallback to default */
                                return settings.monitor >=
                                    0 ? settings.monitor : XDefaultScreen(xctx.dpy);
                        }

                        Window child_return;
                        XTranslateCoordinates(xctx.dpy, focused, root,
                                              0, 0, &x, &y, &child_return);
                }

                for (int i = 0; i < info_len; i++) {
                        if (INRECT(x, y, info[i].x_org,
                                   info[i].y_org,
                                   info[i].width, info[i].height)) {
                                return i;
                        }
                }

                /* something seems to be wrong. Fallback to default */
                return settings.monitor >=
                    0 ? settings.monitor : XDefaultScreen(xctx.dpy);
        }
}
#endif

        /*
         * Update the information about the monitor
         * geometry.
         */
void x_screen_info(screen_info * scr)
{
#ifdef XINERAMA
        int n;
        XineramaScreenInfo *info;
        if ((info = XineramaQueryScreens(xctx.dpy, &n))) {
                int screen = select_screen(info, n);
                if (screen >= n) {
                        /* invalid monitor, fallback to default */
                        screen = 0;
                }
                scr->dim.x = info[screen].x_org;
                scr->dim.y = info[screen].y_org;
                scr->dim.h = info[screen].height;
                scr->dim.w = info[screen].width;
                XFree(info);
        } else
#endif
        {
                scr->dim.x = 0;
                scr->dim.y = 0;

                int screen;
                if (settings.monitor >= 0)
                        screen = settings.monitor;
                else
                        screen = DefaultScreen(xctx.dpy);

                scr->dim.w = DisplayWidth(xctx.dpy, screen);
                scr->dim.h = DisplayHeight(xctx.dpy, screen);
        }
}

        /*
         * Setup X11 stuff
         */
void x_setup(void)
{

        /* initialize xctx.dc, font, keyboard, colors */
        if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
                fputs("no locale support\n", stderr);
        if (!(xctx.dpy = XOpenDisplay(NULL))) {
                die("cannot open display\n", EXIT_FAILURE);
        }

        x_shortcut_init(&settings.close_ks);
        x_shortcut_init(&settings.close_all_ks);
        x_shortcut_init(&settings.history_ks);
        x_shortcut_init(&settings.context_ks);

        x_shortcut_grab(&settings.close_ks);
        x_shortcut_ungrab(&settings.close_ks);
        x_shortcut_grab(&settings.close_all_ks);
        x_shortcut_ungrab(&settings.close_all_ks);
        x_shortcut_grab(&settings.history_ks);
        x_shortcut_ungrab(&settings.history_ks);
        x_shortcut_grab(&settings.context_ks);
        x_shortcut_ungrab(&settings.context_ks);

        xctx.color_strings[ColFG][LOW] = settings.lowfgcolor;
        xctx.color_strings[ColFG][NORM] = settings.normfgcolor;
        xctx.color_strings[ColFG][CRIT] = settings.critfgcolor;

        xctx.color_strings[ColBG][LOW] = settings.lowbgcolor;
        xctx.color_strings[ColBG][NORM] = settings.normbgcolor;
        xctx.color_strings[ColBG][CRIT] = settings.critbgcolor;

        /* parse and set xctx.geometry and monitor position */
        if (settings.geom[0] == '-') {
                xctx.geometry.negative_width = true;
                settings.geom++;
        } else {
                xctx.geometry.negative_width = false;
        }

        xctx.geometry.mask = XParseGeometry(settings.geom,
                                            &xctx.geometry.x, &xctx.geometry.y,
                                            &xctx.geometry.w, &xctx.geometry.h);

        xctx.screensaver_info = XScreenSaverAllocInfo();

        x_win_setup();
        x_cairo_setup();
        x_shortcut_grab(&settings.history_ks);
}

        /*
         * Setup the window
         */
void x_win_setup(void)
{

        printf("x_win_setup\n");
        fflush(stdout);
        Window root;
        XSetWindowAttributes wa;

        xctx.window_dim.x = 0;
        xctx.window_dim.y = 0;
        xctx.window_dim.w = 0;
        xctx.window_dim.h = 0;

        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));
        xctx.utf8 = XInternAtom(xctx.dpy, "UTF8_STRING", false);

        wa.override_redirect = true;
        wa.background_pixmap = ParentRelative;
        wa.event_mask =
            ExposureMask | KeyPressMask | VisibilityChangeMask |
            ButtonPressMask;

        screen_info scr;
        x_screen_info(&scr);
        xctx.win =
            XCreateWindow(xctx.dpy, root, scr.dim.x, scr.dim.y, scr.dim.w,
                          1, 0, DefaultDepth(xctx.dpy,
                                                       DefaultScreen(xctx.dpy)),
                          CopyFromParent, DefaultVisual(xctx.dpy,
                                                        DefaultScreen(xctx.dpy)),
                          CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);
        settings.transparency =
            settings.transparency > 100 ? 100 : settings.transparency;
        setopacity(xctx.win,
                   (unsigned long)((100 - settings.transparency) *
                                   (0xffffffff / 100)));
}

        /*
         * Show the window and grab shortcuts.
         */
void x_win_show(void)
{
        /* window is already mapped or there's nothing to show */
        if (xctx.visible || g_queue_is_empty(displayed)) {
                return;
        }

        x_shortcut_grab(&settings.close_ks);
        x_shortcut_grab(&settings.close_all_ks);
        x_shortcut_grab(&settings.context_ks);

        x_shortcut_setup_error_handler();
        XGrabButton(xctx.dpy, AnyButton, AnyModifier, xctx.win, false,
                    BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
        if (x_shortcut_tear_down_error_handler()) {
                fprintf(stderr, "Unable to grab mouse button(s)\n");
        }

        XMapRaised(xctx.dpy, xctx.win);
        xctx.visible = true;
}

        /*
         * Hide the window and ungrab unused keyboard_shortcuts
         */
void x_win_hide()
{
        x_shortcut_ungrab(&settings.close_ks);
        x_shortcut_ungrab(&settings.close_all_ks);
        x_shortcut_ungrab(&settings.context_ks);

        XUngrabButton(xctx.dpy, AnyButton, AnyModifier, xctx.win);
        XUnmapWindow(xctx.dpy, xctx.win);
        XFlush(xctx.dpy);
        xctx.visible = false;
}

        /*
         * Parse a string into a modifier mask.
         */
KeySym x_shortcut_string_to_mask(const char *str)
{
        if (!strcmp(str, "ctrl")) {
                return ControlMask;
        } else if (!strcmp(str, "mod4")) {
                return Mod4Mask;
        } else if (!strcmp(str, "mod3")) {
                return Mod3Mask;
        } else if (!strcmp(str, "mod2")) {
                return Mod2Mask;
        } else if (!strcmp(str, "mod1")) {
                return Mod1Mask;
        } else if (!strcmp(str, "shift")) {
                return ShiftMask;
        } else {
                fprintf(stderr, "Warning: Unknown Modifier: %s\n", str);
                return 0;
        }

}

        /*
         * Error handler for grabbing mouse and keyboard errors.
         */
static int GrabXErrorHandler(Display * display, XErrorEvent * e)
{
        dunst_grab_errored = true;
        char err_buf[BUFSIZ];
        XGetErrorText(display, e->error_code, err_buf, BUFSIZ);
        fputs(err_buf, stderr);
        fputs("\n", stderr);

        if (e->error_code != BadAccess) {
                exit(EXIT_FAILURE);
        }

        return 0;
}

        /*
         * Setup the Error handler.
         */
static void x_shortcut_setup_error_handler(void)
{
        dunst_grab_errored = false;

        XFlush(xctx.dpy);
        XSetErrorHandler(GrabXErrorHandler);
}

        /*
         * Tear down the Error handler.
         */
static int x_shortcut_tear_down_error_handler(void)
{
        XFlush(xctx.dpy);
        XSync(xctx.dpy, false);
        XSetErrorHandler(NULL);
        return dunst_grab_errored;
}

        /*
         * Grab the given keyboard shortcut.
         */
int x_shortcut_grab(keyboard_shortcut * ks)
{
        if (!ks->is_valid)
                return 1;
        Window root;
        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));

        x_shortcut_setup_error_handler();

        if (ks->is_valid)
                XGrabKey(xctx.dpy, ks->code, ks->mask, root,
                         true, GrabModeAsync, GrabModeAsync);

        if (x_shortcut_tear_down_error_handler()) {
                fprintf(stderr, "Unable to grab key \"%s\"\n", ks->str);
                ks->is_valid = false;
                return 1;
        }
        return 0;
}

        /*
         * Ungrab the given keyboard shortcut.
         */
void x_shortcut_ungrab(keyboard_shortcut * ks)
{
        Window root;
        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));
        if (ks->is_valid)
                XUngrabKey(xctx.dpy, ks->code, ks->mask, root);
}

        /*
         * Initialize the keyboard shortcut.
         */
void x_shortcut_init(keyboard_shortcut * ks)
{
        if (ks == NULL || ks->str == NULL)
                return;

        if (!strcmp(ks->str, "none") || (!strcmp(ks->str, ""))) {
                ks->is_valid = false;
                return;
        }

        char *str = g_strdup(ks->str);
        char *str_begin = str;

        if (str == NULL)
                die("Unable to allocate memory", EXIT_FAILURE);

        while (strstr(str, "+")) {
                char *mod = str;
                while (*str != '+')
                        str++;
                *str = '\0';
                str++;
                g_strchomp(mod);
                ks->mask = ks->mask | x_shortcut_string_to_mask(mod);
        }
        g_strstrip(str);

        ks->sym = XStringToKeysym(str);
        /* find matching keycode for ks->sym */
        int min_keysym, max_keysym;
        XDisplayKeycodes(xctx.dpy, &min_keysym, &max_keysym);

        ks->code = NoSymbol;

        for (int i = min_keysym; i <= max_keysym; i++) {
                if (XkbKeycodeToKeysym(xctx.dpy, i, 0, 0) == ks->sym
                    || XkbKeycodeToKeysym(xctx.dpy, i, 0, 1) == ks->sym) {
                        ks->code = i;
                        break;
                }
        }

        if (ks->sym == NoSymbol || ks->code == NoSymbol) {
                fprintf(stderr, "Warning: Unknown keyboard shortcut: %s\n",
                        ks->str);
                ks->is_valid = false;
        } else {
                ks->is_valid = true;
        }

        free(str_begin);
}

/* vim: set ts=8 sw=8 tw=0: */
