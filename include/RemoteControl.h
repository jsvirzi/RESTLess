#ifndef REMOTECONTROL_H
#define REMOTECONTROL_H

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h> 
#include <poll.h> 
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netdb.h>
#include <inttypes.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
// #include <uchar.h>
typedef unsigned char uchar;

#include <iostream>
#include <vector>
#include <string>

#define MAXSOCKETS 64
class RemoteControl {
	public:

	typedef bool (CallbackFxn)(class RemoteControl *server, int fd, std::vector<std::string> &elements, void *ext);

	typedef struct {
		char *log_buff;
		int reply_buff_length, log_buff_length, *run, *thread_running;
		bool (*log_fxn)(int level, const char *msg);
	} ServerLoopParams;

	typedef struct {
		CallbackFxn *fxn;
		void *ext;
	} CallbackParams;

	enum {
		LOG_LEVEL_DEBUG = 0,
		LOG_LEVEL_WARNING,
		LOG_LEVEL_ERROR,
		LOG_LEVEL_INFO
	};

	RemoteControl(int port, int max_sockets = MAXSOCKETS);
	~RemoteControl();
	bool register_callback(CallbackFxn *fxn, void *ext);
	bool init(unsigned int cpu_mask);

	private: 
	pthread_t tid;
	ServerLoopParams server_loop_params;
	int sleep_wait, run, thread_running;
	int initialize_socket_map();
	int map_socket(int fd);
	int unmap_socket(int fd);
	int get_socket_sunset(int fd);
	int register_socket_sunset(int fd, int timeout);
	int set_nonblocking(int fd);
	bool close();
	bool process();
	bool verbose;
	bool send_minimal_http_reply(int fd, void *buff, int nbytes);
	bool send_minimal_http_image(int fd, std::vector<uchar> &img_buff);
	bool (*log_fxn)(int level, const char *msg);
	private:
	// typedef void *(thread_function)(void *) thread_function;
	void *(server_loop)(void *);
	std::vector<CallbackParams> callback_params;
	char *log_buff;
	int log_buff_length;
};

#endif
