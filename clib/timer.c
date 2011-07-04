#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <unistd.h>

#include "lualock.h"
#include "timer.h"

void timer_new(l_timer_t *timer, unsigned int int_us, int cycles, void (*cb)(void)) {
    timer->cycles = cycles;
    timer->completed_cycles = 0,
    timer->running = false;
    timer->int_us = int_us;
    timer->cb = cb;
    int err = pthread_create(&timer->thread, NULL, timer_run, timer);
    if (err){
        printf("Oops, couldn't make a thread: error %i\n", err);
    }
}
    
void* timer_run(void *data) {
        l_timer_t *timer = (l_timer_t *)data;
        while(timer->cycles == 0 || timer->completed_cycles < timer->cycles) {
            timer->cb();
            usleep(timer->int_us);
            timer->completed_cycles++;
        }
        return NULL;
}

void timer_run_lua_function() {
    lua_getfield(lualock.L, LUA_REGISTRYINDEX, "callback");
    lua_pcall(lualock.L, 0, 0, 0);
}

static int lualock_lua_timer_new(lua_State *L) {
    l_timer_t *timer = lua_newuserdata(L, sizeof(l_timer_t));
    
    int interval = luaL_checknumber(L, 2) * 1000000;
    int run_times = lua_tonumber(L, 3);
    lua_pop(L, 2);
    lua_pushstring(L, "callback");
    lua_insert(L, 1);
    lua_settable(L, LUA_REGISTRYINDEX);
        
    timer_new(timer, interval, run_times, timer_run_lua_function);
    return 1;
}

void lualock_lua_timer_init(lua_State *L) {
        const struct luaL_reg lualock_timer_lib[] =
        {
                { "new", lualock_lua_timer_new },
                { NULL, NULL }
        };
        luaL_register(L, "timer", lualock_timer_lib);
}
