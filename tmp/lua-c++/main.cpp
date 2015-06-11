#include <stdio.h>
#include <stdlib.h>


#include "luainstance.h"


int main( int argc, char *argv[] )
{

  /**
   * Gain access to the single LUA intepretter object.
   */
  CLUAInstance *lua = CLUAInstance::getInstance();

  /**
   * Run a script with it!
   */
  lua->runScript( "lua.lua" );

  {
    int ret = -1;
    lua->callFunction( "test", "s>i", "This is a test string", &ret );
    printf("Result: %d\n", ret);
  }
  /*
   * Cleanup.
   */
  delete( lua );

  return( 0 );
}
