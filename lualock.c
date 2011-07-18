//      lualock.c - A highly configurable screenlocker
//      Copyright ©2011 Guff <cassmacguff@gmail.com>
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.
//      
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//      
//      You should have received a copy of the GNU General Public License
//      along with this program; if not, write to the Free Software
//      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
//      MA 02110-1301, USA.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <clutter-gtk/clutter-gtk.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>
#include <gdk/gdkkeysyms.h>
#include <security/pam_appl.h>
#include <sys/file.h>
#include <errno.h>

#define PW_BUFF_SIZE 32
#define DEFAULT_FONT "Sans 12"

#include "lualock.h"
#include "misc.h"
#include "drawing.h"
#include "lua_api.h"

lualock_t lualock;


time_t test_timer;
int frames_drawn;

struct {
    gboolean no_daemon;
} prefs;

static const char *hook_names[] = { "lock", "unlock", "auth-failed", NULL };

static GOptionEntry options[] = {
    { "no-daemon", 'n', 0, G_OPTION_ARG_NONE, &prefs.no_daemon, "Don't run as a"
        " daemon; lock the screen immediately and exit when done", NULL },
    // get gcc to shut up about missing initializers in this instance
    { NULL, 0, 0, 0, NULL, NULL, NULL }
};

gboolean on_key_press(GtkWidget *widget, GdkEvent *ev, gpointer data);
void show_lock();
void hide_lock();

void init_window() {
    lualock.scr = gdk_screen_get_default();
    lualock.win = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_default_size(GTK_WINDOW(lualock.win),
                                gdk_screen_get_width(lualock.scr),
                                gdk_screen_get_height(lualock.scr));
    GtkWidget *stage_widget = gtk_clutter_embed_new();
    lualock.stage = gtk_clutter_embed_get_stage(GTK_CLUTTER_EMBED(stage_widget));
    gtk_container_add(GTK_CONTAINER(lualock.win), stage_widget);
    g_signal_connect(lualock.win, "key-press-event", G_CALLBACK(on_key_press), NULL);
}

void init_clutter() {
    lualock.pw_actor = clutter_cairo_texture_new(lualock.style.width,
                                                 lualock.style.height);
    clutter_actor_set_position(lualock.pw_actor, lualock.style.x, lualock.style.y);
}

void init_timers() {
    lualock.timers = g_array_new(TRUE, TRUE, sizeof(guint));
}

void init_style() {
    lualock.style.font = DEFAULT_FONT;
    lualock.style.x = 400;
    lualock.style.y = 540;
    lualock.style.off_x = 5;
    lualock.style.off_y = 5;
    lualock.style.width = 200;
    lualock.style.height = 24;
    lualock.style.r = 0;
    lualock.style.g = 0;
    lualock.style.b = 0;
    lualock.style.a = 1;
}

void init_lua() {
    lualock.L = luaL_newstate();
    
    lualock_lua_loadrc(lualock.L);
}

void init_hook_table() {
    lualock.hooks = g_hash_table_new(g_str_hash, g_str_equal);
    
    for (int i = 0; hook_names[i]; i++) {
        GHookList *hook_list = g_malloc(sizeof(GHookList));
        g_hash_table_insert(lualock.hooks, strdup(hook_names[i]), hook_list);
    }
}

void init_hooks() {
    for (int i = 0; hook_names[i]; i++)
        g_hook_list_init(g_hash_table_lookup(lualock.hooks, hook_names[i]),
                         sizeof(GHook));
}

void clear_hooks() {
    for (int i = 0; hook_names[i]; i++)
        g_hook_list_clear(g_hash_table_lookup(lualock.hooks, hook_names[i]));
}

void reset_password() {
    lualock.password[0] = '\0';
    lualock.pw_length = 0;
}

gboolean authenticate_user() {
    return (pam_authenticate(lualock.pam_handle, 0) == PAM_SUCCESS);
}

gboolean on_key_press(GtkWidget *widget, GdkEvent *ev, gpointer data) {
    guint keyval = ((GdkEventKey *)ev)->keyval;
    gchar buf[6];
    guint32 uc;
    guint8 uc_len;
    switch(keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (authenticate_user())
                gtk_main_quit();
            else
                g_hook_list_invoke(g_hash_table_lookup(lualock.hooks, "auth-failed"),
                                   FALSE);
            break;
        case GDK_KEY_BackSpace:
            if (lualock.pw_length > 0)
                lualock.pw_length--;
            lualock.password[lualock.pw_length] = '\0';
            break;
        case GDK_KEY_Escape:
            reset_password();
            break;
        default:
            uc = gdk_keyval_to_unicode(keyval);
            uc_len = g_unichar_to_utf8(uc, buf);
            if (g_unichar_isprint(uc)) {
                // if we're running short on memory for the buffer, grow it
                if (lualock.pw_alloc <= lualock.pw_length + uc_len) {
                    lualock.password = realloc(lualock.password, lualock.pw_alloc + 32);
                    lualock.pw_alloc += 32;
                }
                strncat(lualock.password, buf, uc_len);
                lualock.pw_length += uc_len;
            }
    }
    
    draw_password_mask();
    
    return TRUE;
}

static int pam_conv_cb(int msgs, const struct pam_message **msg,
                       struct pam_response **resp, void *data) {
    *resp = (struct pam_response *) calloc(msgs, sizeof(struct pam_message));
    if (msgs == 0 || *resp == NULL)
        return 1;
    for (int i = 0; i < msgs; i++) {
        if (msg[i]->msg_style != PAM_PROMPT_ECHO_OFF &&
            msg[i]->msg_style != PAM_PROMPT_ECHO_ON)
            continue;

        // return code is currently not used but should be set to zero
        resp[i]->resp_retcode = 0;
        if ((resp[i]->resp = strdup(lualock.password)) == NULL)
            return 1;
    }
    
    reset_password();
    
    return 0;
}

void show_lock() {
    gtk_widget_show_all(lualock.win);
    GdkWindow *win = gtk_widget_get_window(lualock.win);
    gdk_keyboard_grab(win, TRUE, GDK_CURRENT_TIME);
    gdk_pointer_grab(win, TRUE, GDK_BUTTON_PRESS_MASK
                     | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK, NULL,
                     NULL, GDK_CURRENT_TIME);
    draw_password_mask();
   	clutter_actor_raise_top(lualock.pw_actor);
    g_hook_list_invoke(g_hash_table_lookup(lualock.hooks, "lock"), FALSE);
    
    gtk_main();
    hide_lock();
}

void hide_lock() {
    g_hook_list_invoke(g_hash_table_lookup(lualock.hooks, "unlock"), FALSE);
    lua_close(lualock.L);
    clear_timers();
    gtk_widget_hide(lualock.win);
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
    
    clear_hooks();
}

int seconds_idle(Display *dpy, XScreenSaverInfo *xss_info) {
    XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), xss_info);
    return xss_info->idle / 1000;
}

int main(int argc, char **argv) {
    int lock_file = open("/var/lock/lualock.lock", O_CREAT | O_RDWR, 0666);
    int rc = flock(lock_file, LOCK_EX | LOCK_NB);
    if(rc) {
        if(EWOULDBLOCK == errno)
            exit(1);
    }
    GError *error = NULL;
    GOptionContext *opt_context = g_option_context_new("- screenlocker");
    g_option_context_add_main_entries(opt_context, options, NULL);
    g_option_context_add_group(opt_context, gtk_get_option_group(TRUE));
    g_option_context_add_group(opt_context, cogl_get_option_group());
    g_option_context_add_group(opt_context, clutter_get_option_group_without_init());
    g_option_context_add_group(opt_context, gtk_clutter_get_option_group());
    if (!g_option_context_parse(opt_context, &argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }
    
    lualock.password = calloc(PW_BUFF_SIZE, sizeof(char));
    lualock.pw_length = 0;
    lualock.pw_alloc = PW_BUFF_SIZE;
    
    lualock.timeout = 10 * 60;
    
    init_window();
    init_style();
    init_clutter();
    init_timers();
    init_hook_table();
    
    clutter_container_add_actor(CLUTTER_CONTAINER(lualock.stage), lualock.pw_actor);
    
    struct pam_conv conv = {pam_conv_cb, NULL};
    int ret = pam_start("lualock", getenv("USER"), &conv, &lualock.pam_handle);
    // if PAM doesn't get set up correctly, we can't authenticate. so, bail out
    if (ret != PAM_SUCCESS)
        exit(EXIT_FAILURE);
    
    Display *dpy = XOpenDisplay(NULL);
    XScreenSaverInfo *xss_info = XScreenSaverAllocInfo();
    if (prefs.no_daemon) {
        init_hooks();
        init_lua();
        show_lock();
        return 0;
    }
    
    int idle_time;
    while (TRUE) {
        init_hooks();
        init_lua();
        while ((idle_time = seconds_idle(dpy, xss_info)) < lualock.timeout) {
            sleep(lualock.timeout - idle_time - 1);
        }
        show_lock();
    }
    
    return 0;
}
