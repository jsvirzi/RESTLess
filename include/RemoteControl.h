#ifndef MINISERVER_H
#define MINISERVER_H

#include <opencv2/core/core.hpp>
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
#include <uchar.h>

#include <iostream>
#include <vector>
#include <string>

#define MAXSOCKETS 64
class RemoteControl {
	public:

	typedef bool (CallbackFxn)(class RemoteControl *server, int fd, std::vector<std::string> &elements, void *ext);

	typedef struct {
		CallbackFxn *fxn;
		void *ext;
	} CallbackParams;

	typedef struct {
		char *logbuff;
		int port, logbuff_length, sleep_wait, run, thread_started;
	} ServerParams;

	enum {
		LOG_LEVEL_DEBUG = 0,
		LOG_LEVEL_WARNING,
		LOG_LEVEL_ERROR,
		LOG_LEVEL_INFO
	};

	RemoteControl(int port, int max_sockets = MAXSOCKETS);
	~RemoteControl();
	int initialize_socket_map();
	int map_socket(int fd);
	int unmap_socket(int fd);
	int get_socket_sunset(int fd);
	int register_socket_sunset(int fd, int timeout);
	int set_nonblocking(int fd);
	bool close();
	bool process();
	bool verbose;
	bool register_callback(CallbackFxn *fxn, void *ext);
	bool send_minimal_http_reply(int fd, void *buff, int nbytes);
	bool send_minimal_http_image(int fd, std::vector<uchar> &img_buff);
	bool (*log_fxn)(int level, const char *msg);
	private:
	static void server_loop(ServerParams *server_params);
	std::vector<CallbackParams> callback_params;
	int server_version;
	int socket_keep_alive; /* keep a socket alive for 5 seconds after last activity */
/* these two must keep lockstep. jsv make into structures */
	int *socket_index;
	int *socket_sunset;
	int n_sockets;
	char *reply_buffer, *logbuff;
	int reply_buffer_length, logbuff_len;
	struct sockaddr_in cli_addr;
	struct sockaddr_in srv_addr;
	int n, sockfd, port; 
	socklen_t clilen;
	bool status;
/* easier to keep track */
	struct pollfd poll_fd;
	std::vector<struct pollfd> poll_fds;
/* this is what the ioctl call wants to see */
#define POLLSIZE 32
	struct pollfd poll_set[POLLSIZE];
	int num_fds;
};

#endif
