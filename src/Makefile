CC=gcc
CXX=g++
CFLAGS=  -I/usr/local/include -L/usr/local/lib -llua -llualib -lpthread -lpcre -ggdb
OBJECTS=Socket.o  httpbee.o luainterface.o RequestResponseQueue.o Timer.o lrexlib.o
SOURCES=Socket.cpp httpbee.cpp luainterface.cpp RequestResponseQueue.cpp Timer.cpp lrexlib.cpp

all: httpbee

httpbee: $(OBJECTS)
	$(CXX) -o httpbee  $(OBJECTS) $(CFLAGS)


clean:
	rm -f *.o err httpbee
