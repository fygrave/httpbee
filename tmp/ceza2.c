// httpchk.c - fast HTTP/CGI checker by Vladimir <vladimir@beethoven.com>
// compile on Win32 platform: BCC32 -DWIN32 -tWM -O2 HTTPCHK.C
// requires external files: slist.h, strutil.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>             // signal
#include <time.h>               // time, ctime
#ifdef __WIN32
#include <winsock.h>            // WSAStartup, socket
#include <process.h>            // _beginthread, _endthread
#include "..\vputil\slist.h"    // string list + string utils
#else /* unix */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
/* compile with -I ../vputil */
#include "slist.h"
#define closesocket close
#define gets_socket gets
/* etc.. */
#endif

/* constants */
#define PROGNAME    "HTTPCHK"
#define PROGVER     "v0.96"
#define DEFDICTFILE "COMMON"
#define DEFREQFILE  "httpchk.req"
#define DEFLOGFILE  "httpchk.log"
#define DEFERRFILE  "httpchk.err"
#define DEFTHREADS  50
#define MAXTHREADS  60
#define DEFPORT     80
#ifndef SOCKET
#define SOCKET unsigned int
#define SOCKET_ERROR -1
#endif
enum { Q_OK, Q_ABOUT, Q_WSAERR, Q_INPUTERR, Q_DICTERR,
    Q_HOSTSERR, Q_THRDERR, Q_LOGERR, Q_BREAK, Q_SOCKERR, Q_DATAERR };

/* global options, multithread-safe variables */
char **__argv;
int __argc;
char *inputfile     = NULL;
char *nmapfile      = NULL;
char *dictfile      = DEFDICTFILE;
char *reqfile       = DEFREQFILE;
char *logfile       = DEFLOGFILE;
char *errfile       = DEFERRFILE;
char *grepstr       = NULL;
int threadcnt       = DEFTHREADS;
int fullresp        = 0;
int simpleout       = 0;
int showabout       = 0;
int argerror        = 0;
int breakflag       = 0;
int wsa_open        = 0;
int critsect_exist  = 0;
int lists_exist     = 0;
#ifdef __WIN32
WSADATA wsadata;
/* I'd recommend to move threads inteface to separate module and design
 * a set of multiplatform wrappers, so we wouldn't have to have #ifdef
 * the whole code */
HANDLE hthreads[MAXTHREADS];
CRITICAL_SECTION cs_globalcomb, cs_totalsent, cs_totalrecv, cs_output;
#endif
clock_t starttime, elapsedtime;

/* global MT-critical variables */
int globalcomb      = 0;
int totalsent       = 0;
int totalrecv       = 0;

/* global string lists - MT read only */
SLIST input, hosts, words, combined, results, errors;

struct tag_struct {
    int host;
    int word;
    char *responsestr;
};
typedef struct tag_struct *ptag; // to simplify casting

/* declarations and macros */
#define ENTER(csvar) EnterCriticalSection(&csvar)
#define LEAVE(csvar) LeaveCriticalSection(&csvar)

/*--------------------------------------------------------------------------*/

char *getaddress(char *host) {
    char *p = host;
    int ishost = 0;
    struct hostent *h;
    struct in_addr iaddr;
    static char hostaddress[256]; // warning: static

    while (*p) {
        if (!(isdigit(*p) || ((*p) == '.'))) {
            ishost = 1;
            break;
        }
        p++;
    }
    if (ishost) {
        h = gethostbyname(host);
        if (!h) return NULL; // if cannot resolve a hostname
        memcpy((void *)&iaddr.s_addr, (void *)h->h_addr, 4);
        p = inet_ntoa(iaddr);
        if (!p) return NULL; // this would be a stupid error
        strcpy(hostaddress, p);
    } else
        strcpy(hostaddress, host); // copies the same string
    return (hostaddress);
}

int gets_socket(SOCKET s, char *str, int maxlen, int *read) {
    char *endline;

    *read = recv(s, str, maxlen - 1, 0);
    if (!*read || (*read == SOCKET_ERROR))
        return 0;
    ENTER(cs_totalrecv);
    totalrecv += *read;
    LEAVE(cs_totalrecv);
    str[*read] = 0;
    if ((endline = strchr(str, 13)) != NULL)
        *endline = 0;
    if ((endline = strchr(str, 10)) != NULL)
        *endline = 0;
    *read = strlen(str);
    return 1;
}

int puts_socket(SOCKET s, char *str) {
    int written;

    written = send(s, str, strlen(str), 0);
    ENTER(cs_totalsent);
    totalsent += written;
    LEAVE(cs_totalsent);
    return (written == (int)strlen(str));
}

void output(int newline, char *fmt, ...) {
    va_list argptr;
    static int oldsize = 0; // global MT-critical variable
    char outputbuf[512];
    int outputsize;

    va_start(argptr, fmt);
    outputsize = vsprintf(outputbuf, fmt, argptr);
    va_end(argptr);
    ENTER(cs_output);
    if (simpleout) {
        printf("%s\n", outputbuf);
    } else {
        if (outputsize < oldsize) {
            memset(outputbuf + outputsize, ' ', oldsize - outputsize); // fill spaces
            outputbuf[oldsize] = 0; // terminate new output string
        }
        if (newline) {
            printf("%s\n", outputbuf);
            oldsize = 0;
        } else {
            printf("%s\r", outputbuf);
            oldsize = outputsize;
        }
    }
    LEAVE(cs_output);
}

/*--------------------------------------------------------------------------*/

#define BREAKSTAT(val) { exitcode = val; break; }

void scanthread(void *threadno) {
    SOCKET sock;
    struct sockaddr_in saddr;
    int sockopen = 0, curcomb, datalen, exitcode = -1, errindex, match;
    char databuf[1024]; // enough for one line (first line of HTTP response)
    char errstr[256]; // temporary buffer for a connection error string
    LINGER lingerfucker; // linger structure (to avoid TIME_WAIT socket states)

    //output(1, "Init thread [%d]", threadno);

    while ((exitcode == -1) && !breakflag) {

        ENTER(cs_globalcomb);
        if (globalcomb >= combined->count)
            exitcode = 0;
        else
            curcomb = globalcomb++;
        if (!simpleout)
            output(0, "Processing %d%% (request %d of %d)",
                globalcomb * 100 / combined->count, curcomb, combined->count);
        //output(1, "New globalcomb = %d [thread: %d]", globalcomb, threadno);
        LEAVE(cs_globalcomb);

        if (!exitcode) break;

        //output(1, "%s [%d]", combined->str[curcomb], threadno);

        if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) BREAKSTAT(Q_SOCKERR);
        sockopen = 1;

        lingerfucker.l_onoff = 1; // hard close (non-graceful)
        lingerfucker.l_linger = 0; // linger time = 0
        setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&lingerfucker, sizeof(lingerfucker));

        if (breakflag) BREAKSTAT(Q_BREAK);

        saddr.sin_family = AF_INET;
        saddr.sin_port = htons((u_short)INTTAG(hosts, TAG(combined, curcomb, ptag)->host)); // port number
        saddr.sin_addr.s_addr = inet_addr(STR(hosts, TAG(combined, curcomb, ptag)->host)); // from host string (in dotted notation)
        memset(&saddr.sin_zero, 0, 8);

        if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) == SOCKET_ERROR) {
            sprintf(errstr, "Error: cannot connect to %s:%d",
                STR(hosts, TAG(combined, curcomb, ptag)->host),
                INTTAG(hosts, TAG(combined, curcomb, ptag)->host));
            if ((errindex = LFIND(errors, errstr)) == -1) {
                LEXCLUSIVE(errors);
                LADDTAG(errors, errstr, 1);
                LRELEASE(errors);
                output(1, errstr);
            } else {
                LEXCLUSIVE(errors);
                INTTAG(errors, errindex)++; // error counter
                LRELEASE(errors);
            }
            closesocket(sock);
            sockopen = 0;
            continue;
        }

        if (breakflag) BREAKSTAT(Q_BREAK);

        sprintf(databuf, "HEAD /%s HTTP/1.0\n\n", STR(words, TAG(combined, curcomb, ptag)->word));
        if (!puts_socket(sock, databuf)) BREAKSTAT(Q_DATAERR);

        if (breakflag) BREAKSTAT(Q_BREAK);

        match = 0;
        if (gets_socket(sock, databuf, sizeof(databuf), &datalen)) {
            if (grepstr) {
                if (strstrcase(databuf, grepstr))
                    match = 1; // webserver's response matches 'grep' string
            } else {
                if ((strlen(databuf) >= 12) && !strstr(databuf, " 404"))
                    match = 1; // webserver's response differs from "Not found"
            }
            if (match) {
                if (breakflag) BREAKSTAT(Q_BREAK);
                TAG(combined, curcomb, ptag)->responsestr = strdup(databuf + 9);
                output(1, "%s  [%s]", combined->str[curcomb], databuf + 9);
            } else
                TAG(combined, curcomb, ptag)->responsestr = 0;
        }

        if (closesocket(sock))
            output(1, "[%2d] Error: cannot close socket.", threadno);;
        sockopen = 0;

    } // while loop

    if (sockopen)
        if (closesocket(sock))
            output(1, "[%2d] Error: cannot close socket.", threadno);

    switch(exitcode) {
        case Q_SOCKERR:
            output(1, "[%2d] Error: cannot create a socket.", threadno);
            break;
        case Q_DATAERR:
            output(1, "[%2d] Error: cannot send or receive data -- connection error.", threadno);
            break;
    }

    //output(1, "End thread [%d]", threadno);

    _endthread();
}

/*--------------------------------------------------------------------------*/

char *nextargstr(int *argno) {
    if (++(*argno) >= __argc) {
        printf("Error: argument value missing.\n");
        argerror = 1;
        return NULL;
    }
    return (__argv[*argno]);
}

int nextargint(int *argno, int minvalue, int maxvalue) {
    int value;

    if (++(*argno) >= __argc) {
        printf("Error: argument value missing.\n");
        argerror = 1;
        return 0;
    }
    value = atoi(__argv[*argno]);
    if ((value < minvalue) || (value > maxvalue)) {
        printf("Error: argument value out of range.\n");
        argerror = 1;
    }
    return (value);
}

void readargs(void) {
    int i = 1;

    while ((i < __argc) && !argerror && !showabout) {
        if (((__argv[i][0] == '-') || (__argv[i][0] == '/')) && _argv[i][1]) {
            switch (tolower(__argv[i][1])) {
                case 'i':
                    if (inputfile || nmapfile) showabout = 1; else
                    inputfile = nextargstr(&i);
                    break;
                case 'n':
                    if (inputfile || nmapfile) showabout = 1; else
                    nmapfile = nextargstr(&i);
                    break;
                case 'd':
                    if (strcmp(dictfile, DEFDICTFILE)) showabout = 1; else
                    dictfile = nextargstr(&i);
                    break;
                case 'r':
                    if (strcmp(reqfile, DEFREQFILE)) showabout = 1; else
                    reqfile = nextargstr(&i);
                    break;
                case 'o':
                    if (strcmp(logfile, DEFLOGFILE)) showabout = 1; else
                    logfile = nextargstr(&i);
                    break;
                case 't':
                    threadcnt = nextargint(&i, 1, MAXTHREADS);
                    break;
                case 'f':
                    fullresp = 1;
                    break;
                case 'g':
                    if (grepstr) showabout = 1; else
                    grepstr = nextargstr(&i);
                    break;
                case 'p':
                    if (grepstr) showabout = 1; else
                    grepstr = " 200";
                    break;
                case 's':
                    simpleout = 1;
                    break;
                default:
                    showabout = 1;
            }
        } else {
            if (inputfile || nmapfile)
                showabout = 1;
            else
                LADD(input, __argv[i]);
        }
        i++;
    } // while loop
}

void quit(int error) {
    int i;

    switch (error) {
        case Q_ABOUT:
            printf("%s %s - Fast HTTP/CGI checker by Vladimir <vladimir@beethoven.com>\n", PROGNAME, PROGVER);
            printf("Sends requests to the web server (e.g. HEAD /%%dictword%% HTTP/1.0)\n\n");
            printf("%s [[-i inputfile] | [-n nmapfile]] [-d dictfile] [-r reqfile]\n", PROGNAME);
            printf("         [-o logfile] [-t num] [-f] [[-g \"text\"] | [-p]] [-s] [target1]...\n\n");
            printf("  -i inputfile  File containing target hosts, line by line; Use '-' for stdin\n");
            printf("                (IPs or host names, with optional ports, e.g. 10.10.1.2:8000)\n");
            printf("  -n nmapfile  *Import targets from NMAP output file (-oN), all 80/tcp open\n");
            printf("  -d dictfile   File containing words to try (default dictionary '%s')\n", DEFDICTFILE);
            printf("  -r reqfile   *HTTP request/template file to use (default '%s')\n", DEFREQFILE);
            printf("  -o logfile    Output file (default '%s') - will be overwritten\n", DEFLOGFILE);
            printf("  -t num        Number of threads/connections (default: %d, max: %d)\n", DEFTHREADS, MAXTHREADS);
            printf("  -f           *Uses full response from server (not only the first line)\n");
            printf("  -g \"text\"     Outputs only responses containing the specified text (grep)\n");
            printf("  -p            Outputs only \"200 OK\" responses (positive)\n");
            printf("  -s            Simple console write, doesn't update progress info\n");
            printf("  target1 ...   Target host (e.g. 10.10.1.2:8000), multiple hosts allowed\n\n");
            printf("  * options not implemented yet :)\n\n");
            printf("Examples: %s -i targets.txt -o results.txt -p -s\n", PROGNAME);
            printf("          %s 10.10.1.12 10.10.1.13 www.victim.com\n", PROGNAME);
            break;
        case Q_WSAERR:
            printf("Error: cannot initialize Windows Sockets.\n");
            break;
        case Q_INPUTERR:
            printf("Error: input file '%s' is empty, or doesn't exist.\n", inputfile);
            break;
        case Q_DICTERR:
            printf("Error: dictionary file '%s' is empty, or doesn't exist.\n", dictfile);
            break;
        case Q_HOSTSERR:
            printf("Error: list of target IP addresses is empty.\n");
            break;
        case Q_THRDERR:
            printf("Error: unable to create one or more threads.\n");
            break;
        case Q_LOGERR:
            printf("Error: cannot create or write to log file '%s'.\n", logfile);
            break;
        case Q_BREAK:
            printf("Scanning aborted.\n");
            break;
    }

    if (wsa_open) WSACleanup();

    if (lists_exist) {
        LITERATE(combined, i) {
            if (TAG(combined, i, ptag)->responsestr)
                free(TAG(combined, i, ptag)->responsestr);
        }
        LFREE(input);
        LFREE(hosts);
        LFREE(words);
        LFREE(combined);
        LFREE(results);
        LFREE(errors);
    }

    if (critsect_exist) {
        DeleteCriticalSection(&cs_globalcomb);
        DeleteCriticalSection(&cs_totalrecv);
        DeleteCriticalSection(&cs_totalsent);
        DeleteCriticalSection(&cs_output);
    }

    exit(error);
}

void ctrl_c_handler(int signo) {
    if (!breakflag) {
        breakflag = 1;
        output(1, "*BREAK* - program shutdown initiated, please wait...");
        //quit(Q_BREAK);
    }
    signal(SIGINT, ctrl_c_handler);
}

/****************************************************************************/
/*                              MAIN PROGRAM                                */
/****************************************************************************/


int main(int argc, char *argv[]) {
    int i, j, port, newind;
    char *psepar, *numadr;

    InitializeCriticalSection(&cs_globalcomb);
    InitializeCriticalSection(&cs_totalsent);
    InitializeCriticalSection(&cs_totalrecv);
    InitializeCriticalSection(&cs_output);
    critsect_exist = 1;

    signal(SIGINT, ctrl_c_handler);

    input = LCREATE();
    hosts = LCREATE();
    words = LCREATE();
    combined = LCREATE();
    results = LCREATE();
    errors = LCREATE();
    lists_exist = 1;

    input->noempty = 1;
    words->noempty = 1;
    combined->tagalloc = sizeof(struct tag_struct);
    combined->tagclear = 1;

    __argv = argv;
    __argc = argc;
    readargs();
    if (argerror) quit(Q_OK);
    if (showabout || !(inputfile || nmapfile || input->count)) quit(Q_ABOUT);

    if (inputfile) {
        LLOAD(input, inputfile, "");
        LTEXTPROC(input, "W#;\*"); // remove all whitespaces and comments
    } else if (nmapfile) {
        // TODO: import nmap file

    }

    LLOAD(words, dictfile, "");
    LTEXTPROC(words, "W#"); // remove whitespaces and "#" marked words

    if (breakflag) quit(Q_BREAK);
    if (!input->count) quit(Q_INPUTERR);
    if (!words->count) quit(Q_DICTERR);

    if (WSAStartup(0x0101, &wsadata) != 0) quit(Q_WSAERR);
    wsa_open = 1;

    LITERATE(input, i) {
        psepar = strchr(input->str[i], ' '); // 'host port' line format
        if (!psepar)
            psepar = strchr(input->str[i], ':'); // or 'host:port'
        if (!psepar)
            port = DEFPORT; // 80
        else {
            port = atol(psepar + 1);
            *psepar = 0; // temporarily separate host and port
        }
        if (!simpleout)
            output(0, "Enumerating/resolving target hosts (%d): %s", i + 1, input->str[i]);
        numadr = getaddress(input->str[i]);
        if (!numadr) {
            output(1, "Unable to resolve host '%s' - will be skipped", input->str[i]);
            LDEL(input, i--);
            continue;
        }
        LADDTAG(hosts, numadr, port);
        if (psepar)
            *psepar = ':'; // restore host/port delimiter
    }

    if (!simpleout)
        output(0, ""); // clears the current line
    if (breakflag) quit(Q_BREAK);
    if (!hosts->count) quit(Q_HOSTSERR);

    // generate all word:host combinations
    LITERATE(words, j) {
        LITERATE(hosts, i) {
            newind = LADDF(combined, 255, "http://%s/%s", input->str[i], words->str[j]);
            TAG(combined, newind, ptag)->host = i;
            TAG(combined, newind, ptag)->word = j;
        }
    }

    if (threadcnt > combined->count) threadcnt = combined->count;

    output(1, "Initializing scanning engine of %d thread%s", threadcnt, threadcnt > 1 ? "s" : "");
    output(1, "--- Press Ctrl+C to shutdown scanning, or Ctrl+Break in worst case ---");
    output(1, "");

    starttime = clock(); // timer start

    for (i = 0; i < threadcnt; i++)
        /* pthread_create */
        if ((hthreads[i] = (HANDLE)_beginthread(scanthread, 4096, (void *)i)) == INVALID_HANDLE_VALUE)
            quit(Q_THRDERR);

    /* ptread_join */
    WaitForMultipleObjects(threadcnt, hthreads, TRUE, INFINITE);

    elapsedtime = clock() - starttime; // timer stop

    output(1, ""); // clears the current line
    if (breakflag) quit(Q_BREAK);

    LITERATE(combined, i) {
        if (TAG(combined, i, ptag)->responsestr)
            LADDF(results, 1024, "%s  [%s]", combined->str[i], TAG(combined, i, ptag)->responsestr);
    }

    printf("Scanning finished in %.1f seconds - %d URLs found.\n\n",
        elapsedtime / CLOCKS_PER_SEC, results->count);
    printf("Netstat: %.1f KB sent (%.1f KB/sec) and %.1f KB received (%.1f KB/sec).\n",
        (float)totalsent / 1024, totalsent / 1024 * CLOCKS_PER_SEC / elapsedtime,
        (float)totalrecv / 1024, totalrecv / 1024 * CLOCKS_PER_SEC / elapsedtime);
    printf("Average scanning rate: %.1f requests/sec.\n", combined->count * CLOCKS_PER_SEC / elapsedtime);

    if (results->count) {
        LSORT(results, ""); // TODO: real sort by IP addresses
        if (!LSAVE(results, logfile)) quit(Q_LOGERR);
        printf("\nResults saved to file '%s'\n", logfile);
    }
    if (errors->count) {
        LITERATE(errors, i)
            LREPLACEF(errors, i, 256, "%s [occured %d times]", errors->str[i], errors->tag[i]);
        LSORT(errors, ""); // TODO: real sort by IP addresses
        if (LSAVE(errors, errfile))
            printf("\nErrors saved to file '%s'\n", errfile);
    }

    quit(Q_OK);
    return 0; // dummy return
}
