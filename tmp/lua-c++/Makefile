
PROG      = main

OBJS	  = luainstance.o main.o

CPPFLAGS  = -O2 -g `


$(PROG): $(OBJS)
	g++ -o $(PROG) $(OBJS) -L/usr/local/lib -llua -llualib


clean:
	-rm *~ $(PROG) *~ $(OBJS)

