INCLUDES=-Iinclude
LIBS=-Llib -lRESTless
RPATH=$(PWD)/lib
CFLAGS=-ggdb

all: lib/libRESTless.so example/exampleRemoteControl

lib/libRESTless.so: src/RESTless.c include/RESTless.h
	g++ $(CFLAGS) -pthread -shared -fPIC $(INCLUDES) -o lib/libRESTless.so src/RESTless.c

example/exampleRemoteControl: example/exampleRemoteControl.c
	g++ $(CFLAGS) $(INCLUDES) -o example/exampleRemoteControl example/exampleRemoteControl.c $(LIBS) -Wl,-rpath,$(RPATH)

clean:
	rm bin/*
	rm lib/*
	rm example/*

