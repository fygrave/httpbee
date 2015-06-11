Requirements:

    Lua 5.X
    pcre library
    pthreads (for linux)

Tested Platforms:
    Linux
    Windows (coming)
Install:
    edit Makefile. Make sure lua path is correct
    cd src
    make


Usage:

Synopsis:

httpbee [-t threads] [-p port] [-d delay] [-T tag] [-r arg ] [ -v ] -h host -s dir | -D


HttpBee supports two modes: daemon mode (-D) and command line mode.


In command-line mode following arguments are supported
-t threads - pool size of threads to prepare (default 60)
-p target port - default 80
-d delay between requests within single thread. default is none
-T tags - execute scripts that match tag
-r arg - optional argument to pass to each script
-v be verbose
-h target host, in command line mode
-s script dir or filename

The daemon mode is not fully functional. This is for developers reference
only.
In daemon mode(*) no additional arguments are required. Host spec is ignored
-D execute in daemon mode. (detach. listen to commands on port)

TODO:
Implement daemon mode
more modules


Authors:
Meder Kydyraliev (http://o0o.nu)
Fyodor Yarochkin (http://o0o.nu)

Credits:
Philip Sitbon - crossplatform threads
Rene Nyffenegger - original socket class
Vladimir Pudar - original httpchk code
pcrelibrary - lrexlib
