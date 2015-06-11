// httpchk.c - fast HTTP/CGI checker by Vladimir <vladimir@beethoven.com>
// compile on Win32 platform: BCC32 -DWIN32 -tWM -O2 HTTPCHK.C
// requires external files: slist.h, strutil.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>             // signal
#include <time.h>               // clock, time, ctime
#include <winsock.h>            // WSAStartup, socket, connect...
#include <process.h>            // _beginthread, _endthread
#include "..\vputil\slist.h"    // string list + string utils

/* constants */
#define PROGNAME    "HTTPCHK"
#define PROGVER     "v0.99c"
#define DEFDICTFILE "COMMON"
#define DEFREQFILE  "httpchk.req"
#define DEFLOGFILE  "httpchk.log"
#define DEFERRFILE  "httpchk.err"
#define REQVARNAME  "%dictword%"
#define DEFTHREADS  50
#define MAXTHREADS  60
#define DEFPORT     80
enum { Q_OK, Q_ABOUT, Q_ALLOCERR, Q_WSAERR, Q_REQERR, Q_INPUTERR, Q_NMAPERR,
    Q_DICTERR, Q_HOSTSERR, Q_THRDERR, Q_LOGERR, Q_BREAK, Q_SOCKERR, Q_DATAERR };

/* global options, multithread-safe variables */
char **__argv;
int __argc;
char cmdline[1024] = ""; // to be susceptible to buffer overflows :)
char *inputfile     = NULL;
char *nmapfile      = NULL;
char *dictfile      = DEFDICTFILE;
char *reqfile       = DEFREQFILE;
char *logfile       = DEFLOGFILE;
char *errfile       = DEFERRFILE;
char *findstr       = NULL;
char *exclstr       = NULL;
char *requestbuf    = NULL;
char *requestvar    = NULL;
char *starttimestr  = "time";
char *stoptimestr   = "time";
int threadcnt       = DEFTHREADS;
int selectok        = 0;
int fullresp        = 0;
int showstat        = 0;
int simpleout       = 0;
int showabout       = 0;
int argerror        = 0;
int breakflag       = 0;
int wsa_open        = 0;
int critsect_exist  = 0;
int lists_exist     = 0;
WSADATA wsadata;
clock_t starttime, elapsedtime;
time_t datetime;
HANDLE hthreads[MAXTHREADS];
CRITICAL_SECTION cs_globalcomb, cs_totalsent, cs_totalrecv, cs_output;

/* global MT-critical variables */
int globalcomb      = 0;
int totalsent       = 0;
int totalrecv       = 0;

/* global string lists - MT read only */
SLIST input, nmap, hosts, words, combined, results, errors;

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

char *getfilebuffer(char *filename) {
    FILE *fp;
    struct stat statbuf;
    char *buf;
    int bytes;

    fp = fopen(filename, "rb");
    if (!fp) return NULL;
    fstat(fileno(fp), &statbuf);
    buf = malloc(statbuf.st_size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    bytes = fread(buf, 1, statbuf.st_size, fp);
    buf[bytes] = 0;
    fclose(fp);
    if (bytes == statbuf.st_size)
        return (buf);
    else
        return NULL;
}

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

int gets_socket(SOCKET s, char *buf, int maxlen, int lineflag, int *read) {
    char *endline;

    *read = recv(s, buf, maxlen - 1, 0);
    if (!*read || (*read == SOCKET_ERROR))
        return 0;
    ENTER(cs_totalrecv);
    totalrecv += *read;
    LEAVE(cs_totalrecv);
    buf[*read] = 0; // will terminate buffer anyway
    if (lineflag) {
        if ((endline = strchr(buf, 13)) != NULL)
            *endline = 0;
        if ((endline = strchr(buf, 10)) != NULL)
            *endline = 0;
        *read = strlen(buf);
    }
    return 1;
}

int puts_socket(SOCKET s, char *buf) {
    int written;

    written = send(s, buf, strlen(buf), 0);
    ENTER(cs_totalsent);
    totalsent += written;
    LEAVE(cs_totalsent);
    return (written == (int)strlen(buf));
}

void output(int newline, char *fmt, ...) {
    va_list argptr;
    static int oldsize = 0; // global MT-critical variable
    char outputbuf[1024];
    int outputsize;

    if (newline == -1) {
        // special case, to output multiple lines (used in fullresp mode)
        va_start(argptr, fmt);
        ENTER(cs_output);
        if (!simpleout && oldsize) {
            printf("%*s\r", oldsize, ""); // clears the current line
            oldsize = 0;
        }
        vprintf(fmt, argptr);
        puts("");
        LEAVE(cs_output);
        va_end(argptr);
    } else {
        // 'normal' output, one line per time
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
}

/*--------------------------------------------------------------------------*/

#define BREAKSTAT(val) { exitcode = val; break; }

void scanthread(void *threadno) {
    SOCKET sock;
    struct sockaddr_in saddr;
    int sockopen = 0, curcomb, datalen, exitcode = -1, errindex, match;
    char databuf[4096]; // socket data input/output buffer
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
        saddr.sin_port = htons((u_short)TAG(hosts, TTAG(combined, curcomb, ptag)->host)); // port number
        saddr.sin_addr.s_addr = inet_addr(STR(hosts, TTAG(combined, curcomb, ptag)->host)); // from host string (in dotted notation)
        memset(&saddr.sin_zero, 0, 8);

        if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) == SOCKET_ERROR) {
            sprintf(errstr, "Error: cannot connect to %s:%d",
                STR(hosts, TTAG(combined, curcomb, ptag)->host),
                TAG(hosts, TTAG(combined, curcomb, ptag)->host));
            if ((errindex = LFIND(errors, errstr)) == -1) {
                LEXCLUSIVE(errors);
                LADDTAG(errors, errstr, 1);
                LRELEASE(errors);
                output(1, errstr);
            } else {
                LEXCLUSIVE(errors);
                TAG(errors, errindex)++; // error counter
                LRELEASE(errors);
            }
            closesocket(sock);
            sockopen = 0;
            continue;
        }

        if (breakflag) BREAKSTAT(Q_BREAK);

        sprintf(databuf, "HEAD /%s HTTP/1.0\n\n", STR(words, TTAG(combined, curcomb, ptag)->word));
        if (!puts_socket(sock, databuf)) BREAKSTAT(Q_DATAERR);

        if (breakflag) BREAKSTAT(Q_BREAK);

        if (gets_socket(sock, databuf, sizeof(databuf), !fullresp, &datalen)) {
            match = -1;
            if (selectok) {
                // optional "200 OK" filter
                if ((datalen < 12) || strncmp(databuf + 9, "200", 3))
                    match = 0;
            }
            if (match == -1) {
                if (findstr) {
                    // optional FIND string mode
                    if (strstrcase(databuf, findstr))
                        match = 1;
                } else {
                    // default mode, selects all responses != "404 Not found"
                    if ((datalen >= 12) && strncmp(databuf + 9, "404", 3))
                        match = 1;
                }
            }
            if ((match == 1) && exclstr) {
                // optional EXCLUDE string mode
                if (strstrcase(databuf, exclstr))
                    match = 0;
            }
            if (match == 1) {
                if (breakflag) BREAKSTAT(Q_BREAK);
                if (fullresp) {
                    TTAG(combined, curcomb, ptag)->responsestr = strdup(databuf);
                    output(-1, "%s\n%s", combined->str[curcomb], databuf);
                } else {
                    TTAG(combined, curcomb, ptag)->responsestr = strdup(databuf + 9);
                    output(1, "%s  [%s]", combined->str[curcomb], databuf + 9);
                }
            }
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
    int i;
    char arg[40];

    for (i = 1; i < __argc; i++) {
        strcat(cmdline, __argv[i]);
        strcat(cmdline, " ");
    }

    i = 1;
    while ((i < __argc) && !argerror && !showabout) {
        if (((__argv[i][0] == '-') || (__argv[i][0] == '/')) && __argv[i][1]) {
            scopy(arg, __argv[i] + 1);
            strlwr(arg);
            if (!strcmp(arg, "i")) {
                if (inputfile || nmapfile || input->count) showabout = 1; else
                inputfile = nextargstr(&i);
            } else if (!strcmp(arg, "in")) {
                if (inputfile || nmapfile || input->count) showabout = 1; else
                nmapfile = nextargstr(&i);
            } else if (!strcmp(arg, "d")) {
                if (strcmp(dictfile, DEFDICTFILE)) showabout = 1; else
                dictfile = nextargstr(&i);
            } else if (!strcmp(arg, "r")) {
                if (strcmp(reqfile, DEFREQFILE)) showabout = 1; else
                reqfile = nextargstr(&i);
            } else if (!strcmp(arg, "o")) {
                if (strcmp(logfile, DEFLOGFILE)) showabout = 1; else
                logfile = nextargstr(&i);
            } else if (!strcmp(arg, "t")) {
                threadcnt = nextargint(&i, 1, MAXTHREADS);
            } else if (!strcmp(arg, "ok")) {
                selectok = 1;
            } else if (!strcmp(arg, "find")) {
                if (findstr) showabout = 1; else
                findstr = nextargstr(&i);
            } else if (!strcmp(arg, "excl")) {
                if (exclstr) showabout = 1; else
                exclstr = nextargstr(&i);
            } else if (!strcmp(arg, "full")) {
                fullresp = 1;
            } else if (!strcmp(arg, "stat")) {
                showstat = 1;
            } else if (!strcmp(arg, "q")) {
                simpleout = 1;
            } else
                showabout = 1;
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
            printf("Usage: %s [options] [target1] [target2] ....\n\n", PROGNAME);
            printf("  -i inputfile  File containing target hosts, line by line; Use '-' for stdin\n");
            printf("                (IPs or host names, with optional ports, e.g. 10.10.1.2:8000)\n");
            printf("  -in nmapfile  Import targets from NMAP output file (-oN) with 80/tcp open\n");
            printf("  -d dictfile   File containing words to try (default dictionary '%s')\n", DEFDICTFILE);
            printf("  -r reqfile    *HTTP request/template file to use (default '%s')\n", DEFREQFILE);
            printf("  -o logfile    Output file (default '%s') - will be overwritten\n", DEFLOGFILE);
            printf("  -t num        Number of threads/connections (default: %d, max: %d)\n", DEFTHREADS, MAXTHREADS);
            printf("  -ok           Select only \"200 OK\" responses (positive match)\n");
            printf("  -find \"text\"  Select responses containing the specified text\n");
            printf("  -excl \"text\"  Exclude responses containing the specified text\n");
            printf("  -full         Use full responses from server, not only the first line\n");
            printf("  -stat         Display network statistics\n");
            printf("  -q            Simple write to console, doesn't update progress info\n");
            printf("  target1 ...   Target host (e.g. 10.10.1.2:8000), multiple hosts allowed\n\n");
            printf("* Not implemented yet (sorry...)\n\n");
            printf("Examples: %s -i targets.txt -o results.txt -ok\n", PROGNAME);
            printf("          %s 10.10.1.12 10.10.1.13 www.victim.com\n", PROGNAME);
            break;
        case Q_ALLOCERR:
            printf("Error: cannot allocate memory.\n");
            break;
        case Q_WSAERR:
            printf("Error: cannot initialize Windows Sockets.\n");
            break;
        case Q_REQERR:
            printf("Error: HTTP request file '%s' is empty, or doesn't exist.\n", reqfile);
            break;
        case Q_INPUTERR:
            printf("Error: input file '%s' is empty, or doesn't exist.\n", inputfile);
            break;
        case Q_NMAPERR:
            printf("Error: nmap file '%s' has no valid web targets, or doesn't exist.\n", nmapfile);
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

    if (requestbuf) free(requestbuf);

    if (lists_exist) {
        LITERATE(combined, i) {
            if (TTAG(combined, i, ptag)->responsestr)
                free(TTAG(combined, i, ptag)->responsestr);
        }
        LFREE(input);
        LFREE(nmap);
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

void doresults(int emergsave) {
    int i;
    static int saving = 0;

    if (saving) return;
    saving = 1;

    time(&datetime);
    stoptimestr = ctime(&datetime);
    stoptimestr[strlen(stoptimestr) - 1] = 0; // to kill '\n' motherfuckor

    LITERATE(combined, i) {
        if (TTAG(combined, i, ptag)->responsestr) {
            if (fullresp)
                LADDF(results, 5120, "%s\n%s", combined->str[i], TTAG(combined, i, ptag)->responsestr);
            else
                LADDF(results, 1024, "%s  [%s]", combined->str[i], TTAG(combined, i, ptag)->responsestr);
        }
    }

    output(!emergsave && results->count, ""); // clears the current line
    if (!emergsave && breakflag) quit(Q_BREAK);

    if (!emergsave) {
        output(1, "Scanning finished in %.1f seconds, average rate: %.1f requests/sec.",
            elapsedtime / CLOCKS_PER_SEC, combined->count * CLOCKS_PER_SEC / elapsedtime);
        if (showstat)
            output(1, "Netstat: %.1f KB sent (%.1f KB/sec) and %.1f KB received (%.1f KB/sec).",
                (float)totalsent / 1024, totalsent / 1024 * CLOCKS_PER_SEC / elapsedtime,
                (float)totalrecv / 1024, totalrecv / 1024 * CLOCKS_PER_SEC / elapsedtime);
    }

    LSORT(results, "i"); // TODO: real sort by IP addresses
    LINSERTF(results, 0, 1024, "#%s %s initiated on %s with parameters: %s", PROGNAME, PROGVER, starttimestr, cmdline);
    LADDF(results, 1024, "#%s %s on %s", PROGNAME, emergsave ? "aborted" : "finished", stoptimestr);

    if (!LSAVE(results, logfile)) quit(Q_LOGERR);
    output(1, "");
    output(1, "Results saved to file '%s' -- %d URLs found.", logfile, results->count - 2);

    if (errors->count) {
        LITERATE(errors, i)
            LREPLACEF(errors, i, 256, "%s [occured %d times]", errors->str[i], errors->tag[i]);
        LSORT(errors, ""); // TODO: real sort by IP addresses
        if (LSAVE(errors, errfile))
            output(1, "Warning: errors saved to file '%s'", errfile);
    }
}

void ctrl_c_handler(int signo) {
    if (!breakflag) {
        breakflag = 1;
        output(1, "*BREAK* - program shutdown initiated, please wait...");
        doresults(1); // emergency save at Ctrl+C termination
        //quit(Q_BREAK);
    }
    signal(SIGINT, ctrl_c_handler); // why this again?
}

/****************************************************************************/
/*                              MAIN PROGRAM                                */
/****************************************************************************/

int main(int argc, char *argv[]) {
    int i, j, port, newind;
    char *ptarget = NULL, *psepar, *numadr;

    InitializeCriticalSection(&cs_globalcomb);
    InitializeCriticalSection(&cs_totalsent);
    InitializeCriticalSection(&cs_totalrecv);
    InitializeCriticalSection(&cs_output);
    critsect_exist = 1;

    signal(SIGINT, ctrl_c_handler);

    input = LCREATE();
    nmap = LCREATE();
    hosts = LCREATE();
    words = LCREATE();
    combined = LCREATE();
    results = LCREATE();
    errors = LCREATE();
    lists_exist = 1;

    input->noempty = 1;
    nmap->noempty = 1;
    words->noempty = 1;
    combined->tagalloc = sizeof(struct tag_struct);
    combined->tagclear = 1;

    __argv = argv;
    __argc = argc;
    readargs(); // process all command-line stuff
    if (argerror) quit(Q_OK);
    if (showabout || !(inputfile || nmapfile || input->count)) quit(Q_ABOUT);

    //requestbuf = getfilebuffer(reqfile);
    //if (!requestbuf || !strlen(requestbuf)) quit(Q_REQERR);
    //requestvar = strstr(requestbuf, REQVARNAME);
    //if (requestvar)
        //strdel(requestvar, 0, sizeof(REQVARNAME));

    if (inputfile) {
        LLOAD(input, inputfile, "");
        LTEXTPROC(input, "W#;\*"); // remove all whitespaces and comments
    } else if (nmapfile) {
        LLOAD(nmap, nmapfile, "");
        LITERATE(nmap, i) {
            if (strstr(nmap->str[i], "Interesting ports on")) {
                ptarget = strchr(nmap->str[i], '(');
                if (ptarget) {
                    psepar = strchr(ptarget, ')');
                    if (psepar) {
                        *psepar = 0;
                        ptarget++;
                    } else
                        ptarget = NULL;
                }
            } else if (strstr(nmap->str[i], "are: closed"))
                ptarget = NULL;
            else if (strstr(nmap->str[i], "are: filtered"))
                ptarget = NULL;
            else if (strstr(nmap->str[i], "80/tcp     open")) {
                if (ptarget) {
                    LADD(input, ptarget);
                    ptarget = NULL;
                }
            }
        }
        LCLEAR(nmap);
        if (input->count) {
            printf("Imported targets from nmap output file (with 80/tcp open):\n");
            LPRINT(input);
            puts("");
        }
    }

    LLOAD(words, dictfile, "");
    LTEXTPROC(words, "W#"); // remove whitespaces and "#" marked words

    if (breakflag) quit(Q_BREAK);
    if (inputfile && !input->count) quit(Q_INPUTERR);
    if (nmapfile && !input->count) quit(Q_NMAPERR);
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
            output(1, "Unable to resolve host '%s' - will skip it", input->str[i]);
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
            newind = LADDF(combined, 1024, "http://%s/%s", input->str[i], words->str[j]);
            TTAG(combined, newind, ptag)->host = i;
            TTAG(combined, newind, ptag)->word = j;
        }
    }

    if (threadcnt > combined->count) threadcnt = combined->count;

    output(1, "Initializing scanning engine of %d thread%s", threadcnt, threadcnt > 1 ? "s" : "");
    output(1, "--- Press Ctrl+C to shutdown scanning, or Ctrl+Break in worst case ---");
    output(1, "");

    time(&datetime);
    starttimestr = ctime(&datetime);
    starttimestr[strlen(starttimestr) - 1] = 0; // to kill '\n' motherfuckor
    starttime = clock(); // timer start

    for (i = 0; i < threadcnt; i++)
        if ((hthreads[i] = (HANDLE)_beginthread(scanthread, 8192, (void *)i)) == INVALID_HANDLE_VALUE)
            quit(Q_THRDERR);

    WaitForMultipleObjects(threadcnt, hthreads, TRUE, INFINITE);

    elapsedtime = clock() - starttime; // timer stop

    doresults(0); // regular save

    quit(Q_OK);
    return 0; // dummy return
}
