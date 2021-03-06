//      lualock.h - common stuff
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

#ifndef LUALOCK_H
#define LUALOCK_H

#include <lua.h>
#include <gdk/gdk.h>

#define DEFAULT_FONT "Sans 12"

typedef struct {
    PangoFontDescription *font_desc;
    
    gint x;
    gint y;
    
    gint off_x;
    gint off_y;
    
    gint width;
    gint height;
    
    gdouble r;
    gdouble g;
    gdouble b;
    gdouble a;
    
    GdkRGBA bg_color;
    GdkRGBA border_color;
    gdouble border_width;
} style_t;

typedef struct {
    cairo_surface_t *surface;
    gint x;
    gint y;
    gint width;
    gint height;
    gdouble scale_x;
    gdouble scale_y;
    gdouble angle;
    
    gboolean show;
} layer_t;

typedef struct {
    lua_State *L;
    
    GdkScreen *scr;
    GdkWindow *win;
    
    gchar *password;
    gint pw_length;
    gint pw_alloc;
    
    struct pam_handle *pam_handle;
    
    cairo_surface_t *surface_buf;
    cairo_surface_t *surface;
    
    cairo_surface_t *pw_surface;
    
    cairo_surface_t *bg_surface;
    
    GPtrArray *layers;
    cairo_region_t *updates_needed;
    
    gint timeout;
    
    GArray *timers;
    
    guint frame_timer_id;
    
    GHashTable *hooks;
    const gchar **hook_names;
    
    GPtrArray *keybinds;
    
    GMainLoop *loop;
    
    style_t style;
} lualock_t;

extern lualock_t lualock;

void event_handler(GdkEvent *ev, gpointer data);
void reset_password(void);

#endif
