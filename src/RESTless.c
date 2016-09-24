#include <RESTless.h>

#include <stdlib.h>

static bool split(std::vector<std::string> &elements, const char *buffer, int len, const char *delims);

static void *serverLoop(void *ext) {

	RESTless::ServerLoopParams *params = (RESTless::ServerLoopParams *)ext;

	int log_buff_length = params->log_buff_length;
	int reply_buff_length = params->reply_buff_length;
	RESTless *that = params->that;
	char *log_buff = new char [ log_buff_length ]; 
	unsigned char *reply_buff = new unsigned char [ reply_buff_length ]; 

	snprintf(log_buff, log_buff_length, "starting server service");
	params->log_fxn(RESTless::LOG_LEVEL_INFO, log_buff);

	*params->run = 1;
	*params->thread_running = 1;

	while(*params->run) {

		usleep(params->sleep_wait); /* loop pacer */
// printf("serverLoop\n");

		int i, n_read, curtime = time(0);

		if(that->n_sockets > 0) printf("%d sockets open\n", that->n_sockets);

for(i=0;i<that->n_sockets;++i) printf("socket_index(%d) = %d\n", i, that->socket_index[i]);
		that->num_fds = that->poll_fds.size();
for(i=0;i<that->num_fds;++i) printf("poll socket(%d) = %d\n", i, that->poll_fds.at(i).fd);

		/* the first one is always *sockfd* */

		that->num_fds = 0;

		memset(&that->poll_set[that->num_fds], 0, sizeof(pollfd));
		that->poll_set[that->num_fds].fd = that->sockfd;
		that->poll_set[that->num_fds].events = POLLIN;
		++that->num_fds;
		
		std::vector<struct pollfd>::const_iterator it, last = that->poll_fds.end();
		for(it = that->poll_fds.begin();it != last;++it) {
			memset(&that->poll_set[that->num_fds], 0, sizeof(pollfd));
			that->poll_set[that->num_fds].fd = it->fd;
			that->poll_set[that->num_fds].events = it->events;
			++that->num_fds;
		}

		poll(that->poll_set, that->num_fds, 1000);

		that->poll_fds.clear();

		for(i=1;i<that->num_fds;++i) {

			int fd = that->poll_set[i].fd;

		/* check for unused sockets, e.g. - those that have gone some time w/o activity */
			int sunset = that->get_socket_sunset(fd);
			if((0 < sunset) && (sunset < curtime)) {
				that->unmap_socket(fd); /* closes fd */
printf("socket %d timeout. closed\n", fd);
				continue;
			} else if(sunset < 0) {
printf("error: socket %d multiply mapped\n", fd); /* jsv. not sure about what action to take */
			}

		/* nothing to do. put this back into the pool and move on */
			if((that->poll_set[i].revents & POLLIN) == 0) {
				that->poll_fd.fd = that->poll_set[i].fd;
				that->poll_fd.events = POLLIN;
				that->poll_fds.push_back(that->poll_fd);
				continue;
			}

		/* accept any incoming requests. should only happen when i = 0 */ 
			ioctl(that->poll_set[i].fd, FIONREAD, &n_read);
			if(n_read == 0) {
				that->unmap_socket(that->poll_set[i].fd); /* closes fd */
			} else {
			/* new timeout based on recent activity */
				that->registerSocketSunset(that->poll_set[i].fd, that->socket_keep_alive);
				bzero(reply_buff, reply_buff_length);
				int n = read(that->poll_set[i].fd, reply_buff, reply_buff_length-1);
				if(n < 0) {
					snprintf(log_buff, log_buff_length, "ERROR reading from socket");
					if(params->log_fxn) params->log_fxn(RESTless::LOG_LEVEL_WARNING, log_buff);
				}
printf("received message with length = %d: [%s]\n", n, reply_buff);
				that->poll_fd.fd = fd;
				that->poll_fd.events = POLLIN;
				that->poll_fds.push_back(that->poll_fd);

				std::vector<std::string> elements;
				split(elements, (char *)reply_buff, n, " \t\n?;");

// n = elements.size();
// for(i=0;i<n;++i) { printf("element(%d) = [%s]\n", i, elements.at(i).c_str()); }

			/* callbacks */
				std::vector<RESTless::CallbackParams>::const_iterator cbiter, cblast = that->callback_params.end();
				for(cbiter = that->callback_params.begin();cbiter != cblast; ++cbiter) {
					RESTless::CallbackFxn *fxn = cbiter->fxn;
					void *ext = cbiter->ext;
					(*fxn)(that, fd, reply_buff, n, elements, ext);
				}

			}
		}

		/* accept any incoming requests */
		int client_sockfd = accept(that->sockfd, (struct sockaddr *) &that->cli_addr, &that->clilen);
		while(client_sockfd > 0) {
if(that->verbose) printf("accepted connection client = %d bound to server = %d\n", client_sockfd, that->sockfd);
			that->set_nonblocking(client_sockfd);
			memset(&that->poll_fd, 0, sizeof(that->poll_fd));
			that->poll_fd.fd = client_sockfd;
			that->poll_fd.events = POLLIN;
			that->poll_fds.push_back(that->poll_fd);
			that->map_socket(client_sockfd);
			that->registerSocketSunset(client_sockfd, that->socket_keep_alive);
if(that->verbose) printf("adding client on %d (i=%d)\n", client_sockfd, that->poll_set[i].fd);
			client_sockfd = accept(that->sockfd, (struct sockaddr *) &that->cli_addr, &that->clilen);
		}

	}

	delete [] log_buff;
	delete [] reply_buff;

	*params->thread_running = 0;

}

/* parses an integer from a string in the form HEADER=1234.
	input: str is the string to parse
	input: hdr is the string corresponding to the header.
	in the example within this comment str = "HEADER=1234" and hdr = "HEADER" */
bool RESTless::parse_integer(const char *str, const char *hdr, int *result, int dflt) {
	const char *p = strstr(str, hdr);
// printf("%s %s %p\n", str, hdr, p);
	if(p == NULL) {
		if(*result) *result = dflt;
		return false;
	}
	const char *delims = "&;"; /* delimiters for terminating the string */
	int i, j, len = strlen(hdr), n_delims = strlen(delims);
	p += len + 1; /* get past the header + "=" */
	len = strlen(p); /* length of remaining substring after stripping out header */
	char *buff = new char [ len + 1 ];
	memcpy(buff, p, len + 1);
// printf("parse_integer = %s\n", buff);
	for(i=0;i<len;++i) {
		int found = 0;
		for(j=0;j<n_delims;++j) {
			if(buff[i] == delims[j]) {
				buff[i] = 0;
				found = 1;
				break;
			}
		}
		if(found) break;
	}
	i = atoi(buff);
	if(result) *result = i;
	delete [] buff;
	return true;
}

/* parses an integer from a string in the form HEADER=1234.
	input: str is the string to parse
	input: hdr is the string corresponding to the header.
	in the example within this comment str = "HEADER=1234" and hdr = "HEADER" */
bool RESTless::parse_float(const char *str, const char *hdr, float *result, float dflt) {
	const char *p = strstr(str, hdr);
// printf("%s %s %p\n", str, hdr, p);
	if(p == NULL) {
		if(*result) *result = dflt;
		return false;
	}
	const char *delims = "&;"; /* delimiters for terminating the string */
	int i, j, len = strlen(hdr), n_delims = strlen(delims);
	p += len + 1; /* get past the header + "=" */
	len = strlen(p); /* length of remaining substring after stripping out header */
	char *buff = new char [ len + 1 ];
	memcpy(buff, p, len + 1);
// printf("parse_float = %s\n", buff);
	for(i=0;i<len;++i) {
		int found = 0;
		for(j=0;j<n_delims;++j) {
			if(buff[i] == delims[j]) {
				buff[i] = 0;
				found = 1;
				break;
			}
		}
		if(found) break;
	}
	float x = atof(buff);
	if(result) *result = x;
	delete [] buff;
	return true;
}

/* parses a string from a string in the form TEXT="It was the best of times"&AUTHOR="Charles Dickens" (Note: "" are required).
	input: str is the string to parse
	input: hdr is the string corresponding to the header.
	in the example within this comment,
	str = TEXT="It was the best of times"&AUTHOR="Charles Dickens" and hdr = "TEXT".
	the function will return the string "It was the best of times" (no quotes) */
bool RESTless::parse_string(const char *str, const char *hdr, std::string *result, std::string &dflt) {
	const char *p = strstr(str, hdr);
// printf("%s %s %p\n", str, hdr, p);
	if(p == NULL) {
		if(result) *result = dflt;
		return false;
	}
	const char *delims = "&;"; /* delimiters for terminating the string */
	int i, j, len = strlen(hdr), n_delims = strlen(delims);
	p += len + 1; /* get past the header + "=" */
	len = strlen(p); /* length of remaining substring after stripping out header */
	char *buff = new char [ len + 1 ];
	memcpy(buff, p, len + 1);
// printf("parse_string = %s\n", buff);
	for(i=0;i<len;++i) {
		int found = 0;
		for(j=0;j<n_delims;++j) {
			if(buff[i] == delims[j]) {
				buff[i] = 0;
				found = 1;
				break;
			}
		}
		if(found) break;
	}
	if(result) *result = buff;
	delete [] buff;
	return i;
}

static bool is_delimiter(char ch, const char *delims) {
	int n = strlen(delims);
	for(int i=0;i<n;++i) if(ch == delims[i]) return true;
	return false;
}

static bool split(std::vector<std::string> &elements, const char *buffer, int len, const char *delims) {
	if(delims == 0) delims = " \t\n"; /* the defaults */
	if(len == 0) len = strlen(buffer);
	char *str = new char [ len + 1 ];
	int state = 0, i, j;
	for(i=0;i<len;++i) {
		char ch = buffer[i];
		if(state == 0 && !is_delimiter(ch, delims)) {
			j = 0;
			str[j++] = ch;
			state = 1;
		} else if(state == 1 && !is_delimiter(ch, delims)) {
			str[j++] = ch;
		} else if(state == 1) {
			str[j++] = 0;
			elements.push_back(std::string(str));
			state = 0;
		} 
	}
/* finalize state machine. if buffer is not null-terminated, we could be left with a straggler */
	if(state == 1) {
		str[j++] = 0;
		elements.push_back(std::string(str));
	}
	delete [] str;
	return true;
}

bool RESTless::registerCallback(CallbackFxn *fxn, void *ext) {
	CallbackParams params;
	params.fxn = fxn;
	params.ext = ext;
	callback_params.push_back(params);
};

RESTless::~RESTless() {
	delete [] socket_index;
	delete [] socket_sunset;
}

RESTless::RESTless(int port, int max_sockets) {
	status = false;
	server_version = 1;
	socket_keep_alive = 5; /* keep a socket alive for 5 seconds after last activity */
/* these two must keep lockstep. jsv make into structures */
	socket_index = new int [ max_sockets ];
	socket_sunset = new int [ max_sockets ];
	n_sockets = 0;

	this->port = port; 
	memset(&cli_addr, 0, sizeof(struct sockaddr_in));
	memset(&srv_addr, 0, sizeof(struct sockaddr_in));

	log_buff_length = 1024;
	log_buff = new char [ log_buff_length + 1 ];

	log_fxn = 0;

	clilen = sizeof(cli_addr);
 
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	srv_addr.sin_port = htons(port);

	if((sockfd = socket(AF_INET, SOCK_STREAM,0)) < 0) {
		snprintf(log_buff, log_buff_length, "error opening socket");
		if(log_fxn) log_fxn(LOG_LEVEL_ERROR, log_buff);
		status = false;
	}

	if(bind(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
		snprintf(log_buff, log_buff_length, "error opening socket");
		if(log_fxn) log_fxn(LOG_LEVEL_ERROR, log_buff);
		status = false;
	}

	if(listen(sockfd, 64) < 0) {
		snprintf(log_buff, log_buff_length, "error opening socket");
		if(log_fxn) log_fxn(LOG_LEVEL_ERROR, log_buff);
		status = false;
	}

	set_nonblocking(sockfd);

	num_fds = 0;
	memset(poll_set, 0, sizeof(poll_set));
}

bool RESTless::init(unsigned int cpu_mask) {
	const char *name = "\"RESTless\""; // jsv move to include

/* create a new thread for the server loop */
	serverLoopParams.sleep_wait = 250000; /* ms */
	serverLoopParams.log_buff_length = log_buff_length;
	serverLoopParams.log_buff = new char [ serverLoopParams.log_buff_length ]; /* thread-safe, its own buffer */
	serverLoopParams.log_fxn = log_fxn;
	serverLoopParams.thread_running = &thread_running;
	serverLoopParams.reply_buff_length = 1024; // jsv move to configurable 
	serverLoopParams.run = &run;
	serverLoopParams.that = this; 

	int err = pthread_create(&tid, NULL, &serverLoop, (void *)&serverLoopParams);

	if(cpu_mask) { /* are we requesting this thread to be on a particular core? */

		snprintf(log_buff, log_buff_length, "%s configured to run on core mask = 0x8.8%x", name, cpu_mask);
		if(log_fxn) log_fxn(LOG_LEVEL_INFO, log_buff);

		cpu_set_t cpu_set;
		CPU_ZERO(&cpu_set);
		for(int i=0;i<32;++i) { if(cpu_mask & (1 << i)) CPU_SET(i, &cpu_set); }
		err = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpu_set);
		if(err) {
			snprintf(log_buff, log_buff_length, "unable to set thread affinity for %s", name);
			if(log_fxn) log_fxn(LOG_LEVEL_ERROR, log_buff);
		}
		err = pthread_getaffinity_np(tid, sizeof(cpu_set_t), &cpu_set);
		if(err) {
			snprintf(log_buff, log_buff_length, "unable to get thread affinity for %s", name);
			if(log_fxn) log_fxn(LOG_LEVEL_ERROR, log_buff);
		} else {
			for(int i=0;i<CPU_SETSIZE;++i) {
				if(CPU_ISSET(i, &cpu_set)) {
					snprintf(log_buff, log_buff_length, "thread affinity for %s = %d", name, i);
					if(log_fxn) log_fxn(LOG_LEVEL_ERROR, log_buff);
				}
			}
		}
	}

	/* wait for the thread to start */
	int time0 = time(NULL);
	while(thread_running == 0) {
		int dtime = time(NULL) - time0;
		if(dtime > 5) {
			snprintf(log_buff, log_buff_length, 
				"timeout waiting for %s thread to initialize (%d seconds)...", name, dtime);
			if(log_fxn) (*log_fxn)(LOG_LEVEL_ERROR, log_buff);
			return false;
		}
		snprintf(log_buff, log_buff_length, "waiting for %s thread to initialize (%d seconds)...", name, dtime);
		if(log_fxn) (*log_fxn)(LOG_LEVEL_INFO, log_buff);
		usleep(1000000);
	}
	snprintf(log_buff, log_buff_length, "%s thread successfully initialized", name);
	if(log_fxn) log_fxn(LOG_LEVEL_INFO, log_buff);

	return true;
}

#if 0
bool wait_for_thread_to_stop(pthread_t *thread, int *flag, const char *name, int timeout, LogInfo *log) {
	char *thread_result;
	while(*flag) {
		if(time(0) > timeout) {
			snprintf(log->buff, log->buff_length, "CAMERA: %s thread termination timeout", name);
			if(log->fxn) log->fxn(LOG_LEVEL_FATAL, log->buff);
			return false;
		}
		snprintf(log->buff, log->buff_length, "CAMERA: waiting for %s thread to terminate", name);
		if(log->fxn) log->fxn(LOG_LEVEL_INFO, log->buff);
		usleep(100000);
	}

/* and then check the thread function actually returned */
	snprintf(log->buff, log->buff_length, "shutting down %s service", name);
	log->fxn(LOG_LEVEL_INFO, log->buff);
	pthread_join(*thread, (void**) &thread_result);
	snprintf(log->buff, log->buff_length, "CAMERA: %s service stopped", name);
	log->fxn(LOG_LEVEL_INFO, log->buff);

	return true;
}
#endif

int RESTless::initialize_socket_map() {
	int i;
	for(int i=0;i<MAXSOCKETS;++i) socket_index[i] = socket_sunset[i] = 0; 
	n_sockets = 0;
	return 0;
}

int RESTless::map_socket(int fd) {
printf("map_socket(%d)\n", fd);
	int i;
/* make sure this socket isn't already being used */
	for(i=0;i<n_sockets;++i) {
		if(socket_index[i] == fd) return -fd;
	}
	int rc = n_sockets;
	socket_index[n_sockets] = fd;
	socket_sunset[n_sockets] = 0;
	++n_sockets;
	return rc;
}

/* it shouldn't happen, but if it does, 
   unmap_socket() will clean out all instances of the socket fd */
int RESTless::unmap_socket(int fd) {
printf("unmap_socket(%d)\n", fd);
	if(fd <= 0) return -1;
	int i, j, count = 1;
	for(i=0;i<n_sockets;++i) {
		if(socket_index[i] == fd) {
			++count;
			if(count == 1) ::close(fd);
			for(j=i+1;j<n_sockets;++j) {
				socket_index[j-1] = socket_index[j];
				socket_sunset[j-1] = socket_sunset[j];
			}
			--n_sockets;
		} 
	}

	return (count == 1) ? 0 : -1;
}

int RESTless::get_socket_sunset(int fd) {
// printf("get_socket_sunset(%d) n_sockets = %d\n", fd, n_sockets);
	int i, sunset = 0, count = 0;
	for(i=0;i<n_sockets;++i) {
// printf("get_socket_sunset(): socket_index[%d] = %d\n", i, socket_index[i]);
		if(socket_index[i] == fd) {
			sunset = socket_sunset[i];
			++count;
		} 
	}
	if(count > 1) { printf("ERROR: socket multiple entries\n"); }
	return (count > 1) ? -1 : sunset;
}

int RESTless::registerSocketSunset(int fd, int timeout) {
	int i, curtime = time(0), count = 0;
	for(i=0;i<n_sockets;++i) {
		if(socket_index[i] == fd) {
			socket_sunset[i] = curtime + timeout;
			++count;
		}
	}
	return (count > 1) ? -1 : 0;
}

int RESTless::set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if(flags == -1) flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* nbytes indicates how many bytes will be sent immediately after this call */
bool RESTless::send_minimal_http_reply(int fd, char *buff, int nbytes) {
	return send_minimal_http_reply(fd, (unsigned char *)buff, nbytes);
}

bool RESTless::send_minimal_http_reply(int fd, unsigned char *buff, int nbytes) {
	int reply_buff_length = 512;
	char *reply_buff = new char [ reply_buff_length ]; /* jsv. allocate each time? */
	snprintf(reply_buff, reply_buff_length,
		"HTTP/1.1 200 OK\nServer: nauto_server/%d.0\n"
		"Content-Length: %d\nConnection: close\nContent-Type: text/html\n\n", 
		server_version, nbytes); /* header + a blank line */
	write(fd, reply_buff, strlen(reply_buff));
	write(fd, buff, nbytes);
	delete [] reply_buff;
	return true;
}

/* nbytes indicates how many bytes will be sent immediately after this call */
bool RESTless::send_minimal_http_image(int fd, std::vector<uchar> &img_buff) {
	int reply_buff_length = 512;
	char *reply_buff = new char [ reply_buff_length ]; /* jsv. allocate each time? */
	int nbytes = img_buff.size();
	snprintf(reply_buff, reply_buff_length, 
		"HTTP/1.1 200 OK\r\nContent-Type: image/jpg\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n", nbytes);
	int index = strlen(reply_buff);
printf("index=%d nbytes=%d reply_buff_length=%d\n", index, nbytes, reply_buff_length);
	if((index + nbytes) > reply_buff_length) return false;
	unsigned char *p = (unsigned char *)&reply_buff[index];
	unsigned char *p0 = p;
	std::vector<uchar>::const_iterator cit, clast = img_buff.end();
	for(cit=img_buff.begin();cit!=clast;) *p++ = *cit++;
printf("sending http buffer [%s]\n", reply_buff);
printf("write(fd=%d, buff=%p, nwrite=%d);\n", fd, reply_buff, index);
	write(fd, reply_buff, index);
printf("write(fd=%d, buff=%p, nwrite=%d);\n", fd, p0, nbytes);
	write(fd, p0, nbytes);
	delete [] reply_buff;
	return true;
}

bool RESTless::close() {
	std::vector<struct pollfd>::const_iterator it, last = poll_fds.end();
	for(it = poll_fds.begin();it != last;++it) {
		::close(it->fd);
	}

	snprintf(log_buff, log_buff_length, "shutting down server service");
	log_fxn(LOG_LEVEL_INFO, log_buff);
	serverLoopParams.run = 0;
	char *thread_result;
	pthread_join(tid, (void**) &thread_result);
	snprintf(log_buff, log_buff_length, "server service stopped");
	log_fxn(LOG_LEVEL_INFO, log_buff);

	return true;
}

