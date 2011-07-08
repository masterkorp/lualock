#ifndef CLIB_TEXT_H
#define CLIB_TEXT_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

typedef struct {
    const char *text;
    
    double x;
    double y;
    
    const char *font;
    
    double r;
    double g;
    double b;
    double a;
    
    double border_width;
    const char *border_color;
    
    layer_t *layer;
    PangoLayout *layout;
} text_t;

text_t* text_new(text_t *text_obj, const char *text, int x, int y,
                 const char *font, double r, double g, double b, double a,
                 const char *border_color, double border_width);

void lualock_lua_text_init(lua_State *L);

#endif
