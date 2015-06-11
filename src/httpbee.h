#ifndef HTTPBEE_H
#define HTTPBEE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>             // signal
#include <time.h>               // time, ctime
#ifdef __WIN32
#include <winsock.h>            // WSAStartup, socket
#include <process.h>            // _beginthread, _endthread
#else /* unix */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#endif
#endif

