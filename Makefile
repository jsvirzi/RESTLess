INCLUDES=-Iinclude
LIBS=-Llib -lRESTLess
RPATH=$(PWD)/lib
CFLAGS=-ggdb

all: lib/libRESTLess.so example/exampleRemoteControl

lib/libRESTLess.so: src/RESTLess.c include/RESTLess.h
	g++ $(CFLAGS) -pthread -shared -fPIC $(INCLUDES) -o lib/libRESTLess.so src/RESTLess.c

example/exampleRemoteControl: example/exampleRemoteControl.c
	g++ $(CFLAGS) $(INCLUDES) -o example/exampleRemoteControl example/exampleRemoteControl.c $(LIBS) -Wl,-rpath,$(RPATH)

clean:
	rm bin/*
	rm lib/*
	rm example/*

