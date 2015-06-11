
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "luainstance.h"


/*
 * Set our instance pointer to NULL - this will ensure the
 * singleton accessor works as expected.
 */
CLUAInstance * CLUAInstance::m_instance = NULL;



/**
 * Constructor.
 *
 * This isn't public as we're a singleton.
 */
CLUAInstance::CLUAInstance()
{
  /* Create the intepretter object.  */
  m_lua =  lua_open();

  /* Register the standard functions. */
  registerFunctions();
}


/**
 * Destructor.
 */
CLUAInstance::~CLUAInstance()
{
    if ( m_lua != NULL )
    {
        lua_close( m_lua );
	m_lua = NULL;
    }
}


/**
 * Singleton accessor to this class.
 */
/* static */ CLUAInstance *CLUAInstance::getInstance()
{
    if ( !m_instance )
    {
	m_instance = new CLUAInstance( );
    }

    return( m_instance );
}



/**
 * Run the given script.
 */
void CLUAInstance::runScript( const char *fileName )
{
    /* run the script */
    lua_dofile( m_lua, fileName );
}


void CLUAInstance::callFunction(const char *func, const char *sig, ...) 
{
    va_list vl;
    int narg, nres;  /* number of arguments and results */
    
    va_start(vl, sig);
    lua_getglobal( m_lua, func );  /* get function */
    
    /* push arguments */
    narg = 0;
    while (*sig) 
    {  
        /* push arguments */
        switch (*sig++) 
	{
    
          case 'd':  /* double argument */
            lua_pushnumber( m_lua, va_arg(vl, double));
            break;
    
          case 'i':  /* int argument */
            lua_pushnumber( m_lua, va_arg(vl, int));
            break;
    
          case 's':  /* string argument */
            lua_pushstring( m_lua, va_arg(vl, char *));
            break;
    
          case '>':
            goto endwhile;
    
          default:
            callFunctionError("invalid option (%c)", *(sig - 1));
        }
        narg++;
        luaL_checkstack( m_lua, 1, "too many arguments");
    } 
    endwhile:
    

    /* do the call */
    nres = strlen(sig);  /* number of expected results */
    if (lua_pcall( m_lua, narg, nres, 0) != 0)  /* do the call */
      callFunctionError("error running function `%s': %s", func, lua_tostring( m_lua, -1));
    
    /* retrieve results */
    nres = -nres;  /* stack index of first result */
    while (*sig) 
    {  
        /* get results */
        switch (*sig++) 
	{
    
	  case 'd':  /* double result */
            if (!lua_isnumber( m_lua, nres))
              callFunctionError( "wrong result type");
            *va_arg(vl, double *) = lua_tonumber(m_lua, nres);
            break;
    
	  case 'i':  /* int result */
	    if (!lua_isnumber( m_lua, nres))
              callFunctionError("wrong result type");
            *va_arg(vl, int *) = (int)lua_tonumber(m_lua, nres);
            break;
    
          case 's':  /* string result */
            if (!lua_isstring( m_lua, nres))
              callFunctionError("wrong result type");
            *va_arg(vl, const char **) = lua_tostring(m_lua, nres);
            break;
    
          default:
            callFunctionError("invalid option (%c)", *(sig - 1));
        }
        nres++;
    }
    va_end(vl);
}


 
void CLUAInstance::callFunctionError( const char *fmt, ...) 
{
    va_list argp;
    va_start(argp, fmt);
    vfprintf(stderr,fmt, argp);
    va_end(argp);
    lua_close(m_lua);
    exit(EXIT_FAILURE);
}

/**
 * Load the LUA basic functions and register some C callbacks of our own.
 */
void CLUAInstance::registerFunctions()
{
    /*
     * Ensure we have the full LUA basic functions available to us.
     */
    lua_baselibopen(m_lua);
    luaopen_table(m_lua);
    luaopen_io(m_lua);
    luaopen_string(m_lua);
    luaopen_math(m_lua);
    

    /*
     * Register our functions.
     *
     * TODO: This should really be a virtual function in a derived class.
     */
    lua_register(m_lua, "getenv", LUAGetenv);
    lua_register(m_lua, "system", LUASystem);
}




/**
 * A 'getenv' wrapper for LUA scripts.
 */
/* static */ int CLUAInstance::LUAGetenv( lua_State *L )
{
    /* get number of arguments */
    int n = lua_gettop(L);
	
    if ( n != 1 )
    {
        lua_pushstring(L, "One argument is required for 'getenv'");
	lua_error(L);
    }

    if (!lua_isstring(L, 1)) 
    {
        lua_pushstring(L, "String argument required for 'getenv'");
	lua_error(L);
    }

    /* push the average */
    lua_pushstring(L, getenv( lua_tostring( L, 1 ) ) );

    /* return the number of results */
    return 1;
}


/**
 * A 'system' wrapper for LUA scripts.
 */
/* static */ int CLUAInstance::LUASystem( lua_State *L )
{
    /* get number of arguments */
    int n = lua_gettop(L);
    pid_t pid;

#ifdef WAIT_FOR_CHILDREN
    int status;
#endif /* WAIT_FOR_CHILDREN */

    
    if ( n != 1 )
    {
        lua_pushstring(L, "One argument is required for 'system'");
	lua_error(L);
    }

    if (!lua_isstring(L, 1)) 
    {
        lua_pushstring(L, "String argument required for 'system'");
	lua_error(L);
    }


    pid = fork();
    if (pid == -1) 
    {
        lua_pushstring(L, "Fork failed");
	lua_error(L);
    }
    else if (pid == 0) 
    {
        /* code for child */
        execlp(  lua_tostring( L, 1 ), lua_tostring( L, 1 ), NULL );
	_exit(1);  
    }
    else 
    {
#ifdef WAIT_FOR_CHILDREN
        /* parent code: just wait for the child to exit */
        waitpid(pid, &status, WUNTRACED); 
#endif /* WAIT_FOR_CHILDREN */
    }

    /* return the number of results */
    return 0;
}
