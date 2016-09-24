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
typedef unsigned char uchar;

#include <vector>
#include <string>

#define MAXSOCKETS 64
class RESTless {
	public:

	bool parse_string(const char *str, const char *hdr, std::string *result, std::string &dflt);
	bool parse_float(const char *str, const char *hdr, float *result, float dflt);
	bool parse_integer(const char *str, const char *hdr, int *result, int dflt);

	typedef bool (CallbackFxn)(class RESTless *server, int fd, unsigned char *incoming_buffer, int nbytes, std::vector<std::string> &elements, void *ext);

	typedef struct {
		char *log_buff;
		int reply_buff_length, log_buff_length, sleep_wait, *run, *thread_running;
		bool (*log_fxn)(int level, const char *msg);
		RESTless *that; /* this */
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

	RESTless(int port, int max_sockets = MAXSOCKETS);
	~RESTless();
	bool registerCallback(CallbackFxn *fxn, void *ext);
	bool init(unsigned int cpu_mask);

	// private: 
	pthread_t tid;
	ServerLoopParams serverLoopParams;
	int sleep_wait, run, thread_running;
	int initialize_socket_map();
	int map_socket(int fd);
	int unmap_socket(int fd);
	int get_socket_sunset(int fd);
	int registerSocketSunset(int fd, int timeout);
	int set_nonblocking(int fd);
	bool close();
	bool process();
	bool verbose;
	bool send_minimal_http_reply(int fd, char *buff, int nbytes);
	bool send_minimal_http_reply(int fd, unsigned char *buff, int nbytes);
	bool send_minimal_http_image(int fd, std::vector<uchar> &img_buff);
	bool (*log_fxn)(int level, const char *msg);
	// private:
	// typedef void *(thread_function)(void *) thread_function;
	// void *(server_loop)(void *);
	std::vector<CallbackParams> callback_params;
	char *log_buff;
	int log_buff_length;

	int server_version;
	int socket_keep_alive; /* keep a socket alive for 5 seconds after last activity */
/* these two must keep lockstep. jsv make into structures */
	int *socket_index;
	int *socket_sunset;
	int n_sockets;
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
