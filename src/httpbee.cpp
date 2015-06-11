#include <iostream>

using namespace std;
extern "C" {
    #include <stdio.h>
    #include "lua.h"
    #include "lualib.h"
    #include <lauxlib.h>
}
#include "Thread.h"
#include "Mutex.h"
#include "Semaphore.h"
#include "Socket.h"
#include "Options.h"
#include "luainterface.h"
#include "RequestResponseQueue.h"
#include "Timer.h"

#define VERSION "0.1-pre"
#define BANNER "[+] HttpBee "VERSION". (http://o0o.nu)\n"
#define USAGE "Usage: [-t threads] [-p port] [-d delay] [-T tag] [-r arg ] [ -v ] -h host -s dir | -D\n" \
    "HttpBee supports two modes: daemon mode (-D) and command line mode.\n"\
    "In daemon mode no additional arguments are required. Host spec is ignored\n"\
    "In command-line mode following arguments are supported\n"\
    "-t threads - pool size of threads to prepare (default 60)\n"\
    "-p target port - default 80\n"\
    "-d delay between requests within single thread. default is none\n"\
    "-T tags - execute scripts that match tag\n"\
    "-r arg - optional argument to pass to each script\n"\
    "-v be verbose\n"\
    "-h target host, in command line mode\n"\
    "-s script dir or filename\n"\
    "-D execute in daemon mode. (detach. listen to commands on port)\n"
#define MAX_THREADS 255
#define DEFAULT_THREADS 60


typedef Thread<Options> Thread_Scan;
bool done;

int sendrequest(std::string &host, int port, std::string &request) {
        std::string resp = "";
        try {
        SocketClient s(host, port);
        s.SendLine(request);
        s.SendLine("");


        while (1) {
            string l = s.ReceiveLine();
            if (l.empty()) break;
            resp += l;
        }
    }
    catch (const char *s) {
        cerr << s << endl;
    }
    catch (std::string s) {
        cerr << s << endl;
    }
    catch(...) {
        cerr << "unhandled bug!\n";
    }
    RequestResponseQueue *rrq = RequestResponseQueue::getInstance();
    rrq->addResponse(request, resp);
    return 0;
}


void scanthread(Options &arg) {
   // cout << "Scanning " << arg.target << "\n";
    RequestResponseQueue *rrq = RequestResponseQueue::getInstance();
    while (1) {
        if (rrq->getRequestQueueSize() == 0) {
            sleep (1);
            if (!done) continue;
            else break;
        }
        std::string r = rrq->getRequest();
        rrq->incPendingQuery();
        //cout << "got request " << r << "\n";
        sendrequest(arg.target, arg.port, r);
        rrq->decPendingQuery();
        // delay activity, if requested
        if (arg.delay != 0) sleep(arg.delay);
    }

    //cout << "Scanning completed\n";
        

}

void ExecuteScripting(Options &arg) {
    RequestResponseQueue *rrq = RequestResponseQueue::getInstance();
    CLUAInterface lua = CLUAInterface();
    char *buf;
    int ret = -1;

    lua.runScript(arg.script.c_str());
    lua.callFunction( "init", "sis>i", arg.target.c_str(), arg.port, arg.request.c_str(), &ret );

    do {
        lua.callFunction( "request", "sis>s", arg.target.c_str(), arg.port, arg.request.c_str(), &buf );
        if (strlen(buf) != 0) {
            std::string s = buf;
            rrq->addRequest(s);
        }
        // pull all responses
        while (rrq->getResponseQueueSize() != 0) {
            RequestResponse rr = rrq->getResponse();
            lua.callFunction( "response", "siss>i", arg.target.c_str(), arg.port, rr.request.c_str(), rr.response.c_str(), &ret );
        }
    } while(strlen(buf) != 0);

    while (rrq->getResponseQueueSize() != 0 || rrq->getRequestQueueSize() != 0 || rrq->getPendingQueries() != 0) {
        while (rrq->getResponseQueueSize() != 0) {
            if (rrq->getResponseQueueSize() !=0) {
                RequestResponse rr = rrq->getResponse();
                lua.callFunction( "response", "siss>i", arg.target.c_str(), arg.port, rr.request.c_str(), rr.response.c_str(), &ret );
            }
        }
    }

    lua.callFunction( "fini", ">i", &ret );

}

    

int main (int argc, char*  argv[]) {
    int c;
    extern char *optarg;
    Timer tm = Timer();
    Options optz;
    Thread_Scan::Handle H[MAX_THREADS];
    tm.startTimer(); 
    cout << BANNER << "Started at " << tm.startTime() << "\n";

    optz.threads = DEFAULT_THREADS;
    optz.delay = 0;
    while ((c = getopt(argc, argv, "HDvt:d:h:s:p:r:")) != -1) {
        switch(c) {
            case 'D':
                optz.daemon = true;
                 break;
            case 'h':
                optz.target = optarg;
                break;
            case 's':
                optz.script = optarg;
                break;
            case 'p':
                optz.port = atoi(optarg);
                break;
            case 'r':
                optz.request = optarg;
                break;
            case 'v':
                optz.verbose = true;
            case 't':
                optz.threads = atoi(optarg);
                if (optz.threads == 0) {
                    cerr << "Threads number should be integer!\n";
                    exit(0);
                }
                break;
            case 'd':
                optz.delay = atoi(optarg);
                break;
            case 'H':
            default:
                cerr << USAGE << "\n";
                exit(0);
        }
    }

    if (optz.target.length() == 0 && !(optz.daemon)) {
        cerr << USAGE << "\n";
        exit(0);
    }

    RequestResponseQueue::getInstance();
    cout << "Starting up " << optz.threads << " scanning threads...\n";
    done = false;
    for (int i=0; i< optz.threads; i++) {
        if (Thread_Scan::Create((Thread_Scan::Handler)scanthread, optz, &H[i]) )
        { 
            cout << "Creating thread failed!\n";
        }
    }
    ExecuteScripting(optz);
    done = true;
    cout << "Wating for scanning process to stop.\n";
    for (int i=0; i < MAX_THREADS; i++) {
        Thread_Scan::Join(H[i]);
        cout << ".";
        cout.flush();
    }
    tm.stopTimer();
    cout << "done at " << tm.stopTime() << ".\nTotal execution time " << tm.timeDiff() << " seconds.\n";
    return 0;
}
