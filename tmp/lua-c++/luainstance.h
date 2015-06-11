#ifndef _LUAINSTANCE_H
#define _LUAINSTANCE_H 1


/**
 * LUA Is C - protect ourself.
 */
extern "C" 
{
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
}




/**
 * This is the singleton accessor to the LUA instance for an application.
 *
 *
 * @author Steve Kemp
 * @url    http://www.steve.org.uk/
 * @version $Id: $
 */
class CLUAInstance
{

public:
    /**
     * Gain access to the single global instance of this object.
     */
    static CLUAInstance *getInstance();


    /**
     * Destructor.
     */
    ~CLUAInstance( );
    

    /**
     * Run the given script.
     */
    void runScript( const char *fileName );

    /**
     * Call a LUA function from C++.
     *
     * @param func The name of the function to call.
     * @param sig A format string of input and output options with a ">" between them.
     *
     * For example:
     * 
     *  callFunction("f", "dd>d", x, y, &z);
     *
     * Calls a function with two "double" arguments, returning a double.
     *
     * @seealso callFunctionError
     */
    void callFunction(const char *func, const char *sig, ...);


    /**
     * Called if "callFunction" returns an error.
     * @seealso callFunction
     */
    void callFunctionError( const char *fmt, ...);


protected:
    /**
     * Constructor - this is protected as this class uses
     * the singleton design pattern.
     */
    CLUAInstance( );

private:

    /**
     * Register the functions we make available to the scripts
     * executed by "RunScript".
     * @seealso RunScript
     */
    void registerFunctions();


    /**
     * The one and only CIntroScreen object.
     * @seealso getInstance()
     */
    static CLUAInstance *m_instance;


    /**
     * The LUA intepretter object we use and wrap.
     */
    lua_State*  m_lua;


    /**
     * C function to implement the lua "getenv" function.
     */
    static  int LUAGetenv( lua_State *L );

    /**
     * C function to implement the lua "system" function.
     */
    static  int LUASystem( lua_State *L );


};


#endif /* _LUAINSTANCE_H */
