#include <RemoteControl.h>

/* parses an integer from a string in the form HEADER=1234.
	input: str is the string to parse
	input: hdr is the string corresponding to the header.
	in the example within this comment str = "HEADER=1234" and hdr = "HEADER" */
bool parse_integer(const char *str, const char *hdr, int *result, int dflt) {
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
bool parse_float(const char *str, const char *hdr, float *result, float dflt) {
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
bool parse_string(const char *str, const char *hdr, std::string *result, std::string &dflt) {
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

bool RemoteControl::register_callback(CallbackFxn *fxn, void *ext) {
	CallbackParams params;
	params.fxn = fxn;
	params.ext = ext;
	callback_params.push_back(params);
};

RemoteControl::~RemoteControl() {
	delete [] socket_index;
	delete [] socket_sunset;
}

RemoteControl::RemoteControl(int port, int max_sockets) {
	status = false;
	server_version = 1;
	socket_keep_alive = 5; /* keep a socket alive for 5 seconds after last activity */
/* these two must keep lockstep. jsv make into structures */
	socket_index = new int [ max_sockets ];
	socket_sunset = new int [ max_sockets ];
	n_sockets = 0;
	reply_buffer_length = 16384; 
	reply_buffer_length = 4 * 1280 * 960; /* huge for sending images */
	reply_buffer = new char [ reply_buffer_length ];

	this->port = port; 
	memset(&cli_addr, 0, sizeof(struct sockaddr_in));
	memset(&srv_addr, 0, sizeof(struct sockaddr_in));

	logbuff_len = 1024;
	logbuff = new char [ logbuff_len + 1 ];

	log_fxn = 0;

	clilen = sizeof(cli_addr);
 
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	srv_addr.sin_port = htons(port);

	if((sockfd = socket(AF_INET, SOCK_STREAM,0)) < 0) {
		snprintf(logbuff, logbuff_len, "error opening socket");
		if(log_fxn) log_fxn(LOG_LEVEL_ERROR, logbuff);
		status = false;
	}

	if(bind(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
		snprintf(logbuff, logbuff_len, "error opening socket");
		if(log_fxn) log_fxn(LOG_LEVEL_ERROR, logbuff);
		status = false;
	}

	if(listen(sockfd, 64) < 0) {
		snprintf(logbuff, logbuff_len, "error opening socket");
		if(log_fxn) log_fxn(LOG_LEVEL_ERROR, logbuff);
		status = false;
	}

	set_nonblocking(sockfd);

	num_fds = 0;
	memset(poll_set, 0, sizeof(poll_set));
}


int RemoteControl::initialize_socket_map() {
	int i;
	for(int i=0;i<MAXSOCKETS;++i) socket_index[i] = socket_sunset[i] = 0; 
	n_sockets = 0;
	return 0;
}

int RemoteControl::map_socket(int fd) {
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
int RemoteControl::unmap_socket(int fd) {
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

int RemoteControl::get_socket_sunset(int fd) {
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

int RemoteControl::register_socket_sunset(int fd, int timeout) {
	int i, curtime = time(0), count = 0;
	for(i=0;i<n_sockets;++i) {
		if(socket_index[i] == fd) {
			socket_sunset[i] = curtime + timeout;
			++count;
		}
	}
	return (count > 1) ? -1 : 0;
}

int RemoteControl::set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if(flags == -1) flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool RemoteControl::process() {

	int i, n_read, curtime = time(0);

	if(n_sockets > 0) printf("%d sockets open\n", n_sockets);

	for(i=0;i<n_sockets;++i) printf("socket_index(%d) = %d\n", i, socket_index[i]);
	num_fds = poll_fds.size();
	for(i=0;i<num_fds;++i) printf("poll socket(%d) = %d\n", i, poll_fds.at(i).fd);

	/* the first one is always *sockfd* */

	num_fds = 0;

	memset(&poll_set[num_fds], 0, sizeof(pollfd));
	poll_set[num_fds].fd = sockfd;
	poll_set[num_fds].events = POLLIN;
	++num_fds;
		
	std::vector<struct pollfd>::const_iterator it, last = poll_fds.end();
	for(it = poll_fds.begin();it != last;++it) {
		memset(&poll_set[num_fds], 0, sizeof(pollfd));
		poll_set[num_fds].fd = it->fd;
		poll_set[num_fds].events = it->events;
		++num_fds;
	}

	poll(poll_set, num_fds, 1000);

	poll_fds.clear();

	for(i=1;i<num_fds;++i) {

		int fd = poll_set[i].fd;

	/* check for unused sockets, e.g. - those that have gone some time w/o activity */
		int sunset = get_socket_sunset(fd);
		if((0 < sunset) && (sunset < curtime)) {
			unmap_socket(fd); /* closes fd */
printf("socket %d timeout. closed\n", fd);
			continue;
		} else if(sunset < 0) {
printf("error: socket %d multiply mapped\n", fd); /* jsv. not sure about what action to take */
		}

	/* nothing to do. put this back into the pool and move on */
		if((poll_set[i].revents & POLLIN) == 0) {
			poll_fd.fd = poll_set[i].fd;
			poll_fd.events = POLLIN;
			poll_fds.push_back(poll_fd);
			continue;
		}

	/* accept any incoming requests. should only happen when i = 0 */ 
		ioctl(poll_set[i].fd, FIONREAD, &n_read);
		if(n_read == 0) {
			unmap_socket(poll_set[i].fd); /* closes fd */
		} else {
		/* new timeout based on recent activity */
			register_socket_sunset(poll_set[i].fd, socket_keep_alive);
			bzero(reply_buffer, reply_buffer_length);
			n = read(poll_set[i].fd, reply_buffer, reply_buffer_length-1);
			if(n < 0) {
				snprintf(logbuff, logbuff_len, "ERROR reading from socket");
				if(log_fxn) log_fxn(LOG_LEVEL_WARNING, logbuff);
			}
printf("received message: [%s]\n", reply_buffer);
			poll_fd.fd = fd;
			poll_fd.events = POLLIN;
			poll_fds.push_back(poll_fd);

			std::vector<std::string> elements;
			split(elements, reply_buffer, n, " \t\n?;");

			n = elements.size();

// for(i=0;i<n;++i) { printf("element(%d) = [%s]\n", i, elements.at(i).c_str()); }

		/* callbacks */
			std::vector<CallbackParams>::const_iterator cbiter, cblast = callback_params.end();
			for(cbiter = callback_params.begin();cbiter != cblast; ++cbiter) {
				CallbackFxn *fxn = cbiter->fxn;
				void *ext = cbiter->ext;
				(*fxn)(this, fd, elements, ext);
			}

#if 0
			if(n >= 2 && elements[0] == "GET" && elements[1] == "/settings") {
				int olen = 1024;
				char *obuff = new char [ olen + 1 ];
				bool flag;
				snprintf(obuff, olen, "NO PARAMETERS PARSED");
				flag = parse_integer(elements.at(2).c_str(), "exposure", &exposure, exposure);
				if(flag) {
					display_params->exposure = exposure;
					snprintf(obuff, olen, "new exposure set to %d", exposure);
				}
				flag = parse_float(elements.at(2).c_str(), "gamma", &gamma, gamma);
				if(flag) {
					make_gamma_lut(gamma, gamma_lut, GAMMA_LUT_SIZE);
					snprintf(obuff, olen, "gamma set to %f", gamma);
				}
				olen = strlen(obuff);
				snprintf(reply_buffer, reply_buffer_length,
					"HTTP/1.1 200 OK\nServer: nauto_server/%d.0\n"
					"Content-Length: %d\nConnection: close\nContent-Type: text/html\n\n", 
					server_version, olen); /* Header + a blank line */
				write(fd, reply_buffer, strlen(reply_buffer));
				write(fd, obuff, olen);
				delete [] obuff;
			}
#endif
		}
	}

	/* accept any incoming requests */
	int client_sockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
	while(client_sockfd > 0) {
if(verbose) printf("accepted connection client = %d bound to server = %d\n", client_sockfd, sockfd);
		set_nonblocking(client_sockfd);
		memset(&poll_fd, 0, sizeof(poll_fd));
		poll_fd.fd = client_sockfd;
		poll_fd.events = POLLIN;
		poll_fds.push_back(poll_fd);
		map_socket(client_sockfd);
		register_socket_sunset(client_sockfd, socket_keep_alive);
if(verbose) printf("adding client on %d (i=%d)\n", client_sockfd, poll_set[i].fd);
		client_sockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
	}
}

/* nbytes indicates how many bytes will be sent immediately after this call */
bool RemoteControl::send_minimal_http_reply(int fd, void *buff, int nbytes) {
	snprintf(reply_buffer, reply_buffer_length,
		"HTTP/1.1 200 OK\nServer: nauto_server/%d.0\n"
		"Content-Length: %d\nConnection: close\nContent-Type: text/html\n\n", 
		server_version, nbytes); /* header + a blank line */
	write(fd, reply_buffer, strlen(reply_buffer));
	write(fd, buff, nbytes);
	return true;
}

/* nbytes indicates how many bytes will be sent immediately after this call */
bool RemoteControl::send_minimal_http_image(int fd, std::vector<uchar> &img_buff) {
	int nbytes = img_buff.size();
	snprintf(reply_buffer, reply_buffer_length, 
		"HTTP/1.1 200 OK\r\nContent-Type: image/jpg\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n", nbytes);
	// snprintf(reply_buffer, reply_buffer_length,
		// "HTTP/1.1 200 OK\nServer: nauto_server/%d.0\n"
		// "Content-Length: %d\nConnection: close\nContent-Type: text/html\n\n", 
		// server_version, nbytes); /* header + a blank line */
	int index = strlen(reply_buffer);
printf("index=%d nbytes=%d reply_buffer_length=%d\n", index, nbytes, reply_buffer_length);
	if((index + nbytes) > reply_buffer_length) return false;
	unsigned char *p = (unsigned char *)&reply_buffer[index];
	unsigned char *p0 = p;
	std::vector<uchar>::const_iterator cit, clast = img_buff.end();
	for(cit=img_buff.begin();cit!=clast;) *p++ = *cit++;
printf("sending http buffer [%s]\n", reply_buffer);
printf("write(fd=%d, buff=%p, nwrite=%d);\n", fd, reply_buffer, index);
	write(fd, reply_buffer, index);
printf("write(fd=%d, buff=%p, nwrite=%d);\n", fd, p0, nbytes);
	write(fd, p0, nbytes);
	return true;
}

bool RemoteControl::close() {
	std::vector<struct pollfd>::const_iterator it, last = poll_fds.end();
	for(it = poll_fds.begin();it != last;++it) {
		::close(it->fd);
	}
	return true;
}

void *RemoteControl::server_loop(ServerParams *params) {

	int logbuff_length = (params->logbuff_length == 0) ? 1024 : params->logbuff_length;
	char *logbuff = (params->logbuff == 0) ? new char [ logbuff_length ] : params->logbuff;
	int sleep_wait = (params->sleep_wait == 0) ? 1000000 : params->sleep_wait; /* default = 1 second */

	snprintf(logbuff, logbuff_length, "starting server service");
	log_fxn(LOG_LEVEL_INFO, logbuff);

	RemoteControl::CallbackExt server_callback_ext;

	RemoteControl *server = new RemoteControl(params->port);
	server_callback_ext.obuff_len = 1024;
	server_callback_ext.obuff = new char [ server_callback_ext.obuff_len ];
	server->register_callback(process_incoming_http, &server_callback_ext);
	server->log_fxn = log_fxn;
 
	params->run = 1;
	params->thread_started = 1;

	while(params->run) {
		usleep(sleep_wait);
		server->process();
	}

	server->close();
	delete server;

	if(params->logbuff == 0) delete [] logbuff; /* if input == 0, we allocated */
	delete [] server_callback_ext.obuff;

	params->thread_started = 0;
}

