#include <stdlib.h>
#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>


int main(void) {
static const luaL_reg lualibs[] =
    {
       { "base", luaopen_base },
       { NULL, NULL }
    };
    static void openlualibs(lua_State *l) {
        const luaL_reg *lib;

        for (lib = lualibs; lib->func != NULL; lib++) {
            lib->func(l);
            lua_settop(l, 0);
        }
    }


    lua_State *l;
    l = lua_open();
    openlualibs(l);

    lua_dofile(l, "script.lua");
    lua_close(l);
    return 0;


}
