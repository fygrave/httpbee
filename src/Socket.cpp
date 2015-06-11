#include "Socket.h"
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#ifndef TIMEVAL
#define TIMEVAL struct timeval
#endif

#include <iostream>
#define BUFFER_SIZE 10024


using namespace std;
#ifndef __WIN32
#include <errno.h>
#endif

int Socket::nofSockets_ = 0;

void Socket::Start() {
#ifdef __WIN32
    if (!nofSockets_) {
        WSADATA info;
        if (WSAStartup(MAKEWORD(2,0). &info)) {
            throw "Could not start WSA";
        }
    }
#endif
    // should have semaphore lock here..
    ++nofSockets_;
}

void Socket::End() {
#ifdef __WIN32
    WSACleanup();
#endif
}

Socket::Socket() : s_(0) {
    Start();
    s_ = socket(AF_INET, SOCK_STREAM, 0);
    if (s_ < 0) {
        throw "INVALID_SOCKET";
    }

    refCounter_ = new int (1);
}

Socket::Socket(SOCKET s) : s_(s) {
    Start();
    refCounter_ = new int(1);
};

Socket::~Socket() {
    if (! --(*refCounter_)) {
        Close();
        delete refCounter_;
    }
    --nofSockets_;
    if (!nofSockets_) End();
    
}

Socket::Socket(const Socket& o) {
    refCounter_ = o.refCounter_;
    (*refCounter_)++;
    s_ = o.s_;
    nofSockets_++;
}

Socket& Socket::operator=(Socket& o) {
    (*o.refCounter_)++;

    refCounter_ = o.refCounter_;
    s_ = o.s_;
    nofSockets_++;

    return *this;
}
void Socket::Close() {
    closesocket(s_);
}

std::string Socket::ReceiveBytes() {

    std::string ret;
    char buf[BUFFER_SIZE];

    while(1) {
        u_long arg = 0;
#ifdef __WIN32
        if (ioctlsocket(s_, FIONREAD, &arg) != 0)
            break;
#else
        if (arg = recv(s_, buf, BUFFER_SIZE, MSG_PEEK) !=0)
            break;
#endif

        if (arg == 0)
            break;
        if (arg > BUFFER_SIZE) arg = BUFFER_SIZE;
        int rv = recv(s_, buf, arg, 0);
        if (rv < 0) break;

        std::string t;

        t.assign(buf, rv);
        ret += t;
    }
    return ret;
}

std::string Socket::ReceiveLine() {
    std::string ret = "";

    while (1) {
        char r;

        switch (recv(s_, &r, 1, 0)) {
            case 0: // connection dropped
                return ret;
            case -1:
                return ret;
        }
        ret += r;
        if (r == '\n') return ret;
    }
}

int Socket::SendLine(std::string s) {
    s+= '\n';
    return (send(s_, s.c_str(), s.length(), 0));
}

int Socket::SendBytes(const std::string& s) {
   return(s_, s.c_str(), s.length(), 0);
}

SocketServer::SocketServer(int port, int connections, TypeSocket type) {
    sockaddr_in sa;

    memset(&sa, 0, sizeof(sa));

    sa.sin_family = PF_INET;
    sa.sin_port = htons(port);
    s_ = socket(AF_INET, SOCK_STREAM, 0);
    if (s_ < 0 ) {
        throw "INVALID_SOCKET";
    }
    if (type == NonBlockingSocket) {
        u_long arg = 1;
#ifdef __WIN32
        ioctlsocket(s_, FIONBIO, &arg);
#else
        // FIOSNBIO
      //  ioctl(s_, FIONBIO, &arg);
        fcntl(s_, F_SETFL, O_NDELAY);
        fcntl(s_, F_SETFL, O_NONBLOCK);
#endif
    }
    if (bind(s_, (sockaddr *) &sa, sizeof(sockaddr_in)) < 0) {
        closesocket(s_);
        throw "INVALID_SOCKET";
    }
    listen(s_, connections);
}

Socket* SocketServer::Accept() {
    SOCKET new_sock = accept(s_, 0, 0);
    if (new_sock < 0 ) {
        int rc;
#ifdef __WIN32
        rc = WSAGetLastError();
        if (rc == WSAEWOULDBLOCK) {
            return 0;
        } else {
            throw "INVALID_SOCKET";
        }
#else  
        rc = errno;
        if (rc == EWOULDBLOCK) {
            return 0;
        } else {
            throw "INVALID_SOCKET";
        }
#endif
    }
    Socket* r = new Socket(new_sock);
    return r;
}

SocketClient::SocketClient(const std::string& host, int port) : Socket () {
    std::string error;

    hostent *he;
    if ((he = gethostbyname(host.c_str())) == 0) {
        error = strerror(errno);
        throw error;
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *((in_addr *)he->h_addr);
    memset(&(addr.sin_zero), 0, 8);

    if (::connect(s_, (sockaddr *) &addr, sizeof(sockaddr))) {
#ifdef __WIN32
        error = strerror(WSAGetLastError());
#else
        error = strerror(errno);
#endif 
        throw error;
    }
}

SocketSelect::SocketSelect(Socket const * const s1, Socket const * const s2, TypeSocket type) {
    FD_ZERO(&fds_);
    FD_SET(const_cast<Socket*>(s1)->s_, &fds_);
    if (s2) {
        FD_SET(const_cast<Socket*>(s2)->s_, &fds_);
    }
    TIMEVAL tval;
    tval.tv_sec = 0;
    tval.tv_usec = 1;

    TIMEVAL *ptval;
    if (type == NonBlockingSocket) {
        ptval = &tval;
    } else {
        ptval = 0;
    }

    if (select(0, &fds_, (fd_set *)0, (fd_set *)0, ptval) < 0) {
        throw "SELECT_ERROR";
    }
}


bool SocketSelect::Readable(Socket const* const s) {
    if (FD_ISSET(s->s_, &fds_)) return true;
    return false;
}
