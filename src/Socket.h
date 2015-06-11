#ifndef SOCKET_H
#define SOCKET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>             // signal
#include <time.h>               // time, ctime
#ifdef __WIN32
#include <winsock.h>            // WSAStartup, socket
#else /* unix */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#ifndef SOCKET
#define SOCKET int
#endif
#ifndef closesocket
#define closesocket close
#endif
#include <string>

enum TypeSocket {BlockingSocket, NonBlockingSocket};

class Socket {
    public:
        virtual ~Socket();
        Socket(const Socket&);
        Socket& operator=(Socket&);

        std::string ReceiveLine();
        std::string ReceiveBytes();

        void Close();

        // SendLine appends 
        // '\n' to the string
        int SendLine(std::string);
        // SendBytes does not modify string
        int SendBytes(const std::string&);

    protected:
        friend class SocketServer;
        friend class SocketSelect;
        Socket(SOCKET s);
        Socket();

        SOCKET s_;
        int* refCounter_;
    private:
        static void Start();
        static void End();
        static int nofSockets_;
};

class SocketClient: public Socket {
    public:
        SocketClient (const std::string& host, int port);
};

class SocketServer : public Socket {
    public:
        SocketServer(int port, int connections, TypeSocket type=BlockingSocket);
        Socket* Accept();
};

class SocketSelect {
    public:
        SocketSelect(Socket const * const s1, Socket const * const s2=NULL, TypeSocket type=BlockingSocket);
        bool Readable(Socket const * const s);

        private:
        fd_set fds_;
};
#endif
