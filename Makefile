INCLUDES=-Iinclude
LIBS=-Llib -lRemoteControl
RPATH=$(LIBS)
CFLAGS=-ggdb

all: lib/libRemoteControl.so example/exampleRemoteControl

lib/libRemoteControl.so: src/RemoteControl.c include/RemoteControl.h
	g++ $(CFLAGS) -pthread -shared -fPIC $(INCLUDES) -o lib/libRemoteControl.so src/RemoteControl.c

example/exampleRemoteControl: example/exampleRemoteControl.c
	g++ $(CFLAGS) $(INCLUDES) -o example/exampleRemoteControl example/exampleRemoteControl.c $(LIBS) -Wl,-rpath,$(RPATH)

clean:
	rm bin/*
	rm lib/*
	rm example/*

