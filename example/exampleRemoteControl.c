#include <RESTless.h>

#include <syslog.h>
#include <stdlib.h>
#include <signal.h>

static bool running = true;

/* when we hit ^C tell main loop to exit */
static void stop(int sig) {
	syslog(LOG_INFO, "%d signal received", sig);
	running = false;
}

bool debug = false, verbose = false;

const int log_buff_length = 1024;
char log_buff[log_buff_length];
bool log_fxn(int level, const char *msg) {
	printf("logger: level = %d. message = [%s]\n", level, msg);
}

/*** begin IPC ***/

typedef struct {
	int fd, state, nbytes, bytes_expected, boundary_size;
	unsigned char *buff, *boundary;
} RESTlessCallbackExt;

#if 0

// POST / HTTP/1.1
// User-Agent: curl/7.35.0
// Host: localhost:8100
// Accept: */*
// Content-Length: 226
// Expect: 100-continue
// 84 chars // Content-Type: multipart/form-data; boundary=------------------------f9bf9d3c2e8fc08a
// 
// 42 chars // --------------------------f9bf9d3c2e8fc08a
// 71 chars // Content-Disposition: form-data; name="fileupload"; filename="test.html"
// 23 chars // Content-Type: text/html
// 
// 34 chars // Welcome, my friend, to the machine
// 44 chars // --------------------------f9bf9d3c2e8fc08a--
// 

#endif

bool parse_field(const char *msg, const char *field, char *str, int max_chars) {
	std::string haystack = msg, needle = field;
	size_t pos = haystack.find(needle);
	if(pos == std::string::npos) return false;
	pos = pos + needle.length();
	bool last = (haystack[pos] == 0) || (haystack[pos] == 0xd) || (haystack[pos] == 0xa) || (haystack[pos] == ';');
	int i;
	for(i=0;i<(max_chars-1)&&!last;++i) { /* reserve room for null termination */
		str[i] = haystack[pos];
		++pos;
		last = (haystack[pos] == 0) || (haystack[pos] == 0xd) || (haystack[pos] == 0xa) || (haystack[pos] == ';');
	}
	str[i] = 0;
	// printf("parse_field(%s, %s, %p, %d) returns %s\n", msg, field, str, max_chars, str); 
	return true;
}

bool send_minimal_http_continue(int fd) {
	int reply_buff_length = 512;
	char *reply_buff = new char [ reply_buff_length ]; /* jsv. allocate each time? */
	snprintf(reply_buff, reply_buff_length, "HTTP/1.1 100 Continue\n\n");
	write(fd, reply_buff, strlen(reply_buff));
	delete [] reply_buff;
	return true;
}

bool process_incoming_http(RESTless *server, int fd, unsigned char *incoming_buffer, int nbytes, std::vector<std::string> &elements, void *ext) {

	RESTlessCallbackExt *params = (RESTlessCallbackExt *)ext;
	std::string haystack, needle;
	unsigned char *str = new unsigned char [ nbytes + 1 ];
	unsigned char *tmp = new unsigned char [ nbytes + 1 ];
	int n = elements.size();
	int offset = 0;
	unsigned char *where = incoming_buffer + offset;

	if(params->fd == fd) {
printf("callback with %d bytes. state = %d\n", nbytes, params->state);
		if(params->state == 1) {
			offset += 2;
			where = incoming_buffer + offset;
			memcpy(str, where, params->boundary_size);
			str[params->boundary_size] = 0;
			if(memcmp(params->boundary, str, params->boundary_size) == 0) {
printf("first boundary found\n");
				offset += (params->boundary_size + 2); /* get past newline = 0xd 0xa */
				where = incoming_buffer + offset;
				params->state = 2; /* look for download metadata */
			} else {
printf("boundary = [%s]\n", params->boundary);
printf("what i found = [%s]\n", str);
			}
		}
		if(params->state == 2) {
printf("remaining buffer = [%s]\n", where);
			const char *header = "Content-Disposition: ";
			parse_field((char *)where, header, (char *)str, nbytes);
printf("%s => [%s]\n", header, str);
			header = "filename=";
			parse_field((char *)where, header, (char *)str, nbytes);
printf("%s => [%s]\n", header, str);
			snprintf(tmp, "%s%s", header, str);
// for(int i=0;i<strlen((char *)where);++i) { printf("%2x ", where[i]); }

			haystack = (char *)where, needle = "\x0d\x0a\x0d\x0a";
			size_t pos = haystack.find(needle);
			if(pos != std::string::npos) {
				pos += sizeof(needle);
				offset += pos;
				where += pos;
printf("pos = %ld. remaining buffer = [%s]\n", pos, where);
			} else {
printf("me no findy\n");
			}

		}

		memcpy(params->buff + params->nbytes, incoming_buffer, params->nbytes);
		params->nbytes += nbytes;
		if(params->nbytes >= params->bytes_expected) {
			printf("we are done %d vs %d\n", params->nbytes, params->bytes_expected);
			params->fd = 0;
		}
	}

	delete [] str;
	delete [] tmp;

	if(n < 2) { return false; } /* nothing useful */

/* POST messages */

printf("=== [%s] === [%s] ===\n", elements[0].c_str(), elements[1].c_str());
	
	if(elements[0] == "POST" && elements[1] == "/upload.html") {
printf("received POST\n");
		char str[1024];
		str[0] = 0;
		const char *header = "Content-Length: ";
		parse_field((char *)incoming_buffer, header, str, sizeof(str));
		int nbytes = atoi(str);
printf("%s => [%s] = %d\n", header, str, nbytes);
		params->fd = fd;
		params->nbytes = 0;
		params->bytes_expected = nbytes;
		params->buff = new unsigned char [ nbytes ];
		header = "Content-Disposition: ";
		parse_field((char *)incoming_buffer, header, str, sizeof(str));
printf("%s => [%s]\n", header, str);
		header = "Content-Type: ";
		parse_field((char *)incoming_buffer, header, str, sizeof(str));
printf("%s => [%s]\n", header, str);
		header = "boundary=";
		parse_field((char *)incoming_buffer, header, str, sizeof(str));
printf("%s => [%s]\n", header, str);
// multipart/form-data; boundary
		char *boundary = str;
		nbytes = strlen(boundary);
		params->boundary_size = nbytes;
		params->boundary = new unsigned char [ params->boundary_size + 1 ];
		memcpy(params->boundary, boundary, params->boundary_size + 1);

		params->state = 1; /* sync to header */

		send_minimal_http_continue(fd);
		return true;
	} 

	if(elements[0] != "GET") { return false; } /* this is all we're using for now */

	if(elements[1] == "/test.html") {
		const char *test_string = "Welcome, my friend, to the machine!";
		int obuff_len = strlen(test_string);
		char *obuff = new char [ obuff_len ]; 
		snprintf(obuff, obuff_len, "%s", test_string); 
		server->send_minimal_http_reply(fd, obuff, strlen(obuff), true); // delete buff after send
		return true;
	}

	if(n < 3) { /* from here out, we need some arguments in the form key=value */
		return false;
	}

	if(elements[1] == "/control.html") {
		int obuff_len = 256;
		char *obuff = new char [ obuff_len ]; 
		snprintf(obuff, obuff_len, "NO PARAMETERS PARSED");
		int dac_setting = -1;
		bool flag = server->parse_integer(elements.at(2).c_str(), "dac", &dac_setting, dac_setting);
		if(flag && (dac_setting > 0)) {
			snprintf(obuff, obuff_len, "new dac setting = %d", dac_setting);
			server->send_minimal_http_reply(fd, obuff, strlen(obuff), true); // delete buff after send
			return true;
		}
		delete [] obuff;
	}

	return true;
}

/*** final IPC ***/

int main(int argc, char **argv) {

	int i, port = 8080;

	for(i=1;i<argc;++i) {
		syslog(LOG_NOTICE, "%d: %s", i, argv[i]);
		if(strcmp(argv[i], "-debug") == 0) debug = true;
		else if(strcmp(argv[i], "-verbose") == 0) verbose = true;
		else if(strcmp(argv[i], "-port") == 0) port = atoi(argv[++i]);
	}

	signal(SIGINT, stop); /* ^C  exception handling */ 
	signal(SIGTERM, stop); /* exception handling */ 

	RESTless *remote_control = new RESTless(port);
	RESTlessCallbackExt callback_ext;
	callback_ext.fd = 0;
	callback_ext.state = 0;
	callback_ext.nbytes = 0;
	remote_control->register_callback(process_incoming_http, &callback_ext);

	remote_control->log_fxn = log_fxn;
	remote_control->init(0);

	while(running) {
		sleep(1);
	}

	remote_control->close();
	delete remote_control;

}

#if 0








#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
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
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <netinet/in.h>
#include <netdb.h>
#include <inttypes.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <iostream>
#include <vector>
#include <string>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

#include <camera.h>
#include <nauto.h>
#include <gimp_client.h>
#include <nauto_utils.h>
#include <miniserver.h>
#include <session.h>
#include <xu4.h>
#include <video_compression.h>

typedef struct {
	int height, width, wait, gamma_lut_size;
//	int exposure, current_exposure, set_exposure_countdown;
	const char *window_name;
	unsigned char *work_buffer;
	unsigned char *gamma_lut;
	float gamma;
	RegionOfInterest *roi;
} DisplayParams;

typedef struct {
	char *logbuff;
	int port, logbuff_length, sleep_wait;
	int run, thread_started;
	DisplayParams *display_params;
	Camera *camera;
} ServerParams;

bool log_fxn(int level, const char *msg) {
	printf("logger: level = %d. message = [%s]\n", level, msg);
	syslog(LOG_NOTICE, "logger: level = %d. message = [%s]\n", level, msg);
}

enum {
	SessionStateIdle = 0,
	SessionStatePending,
	SessionStateRunning,
	SessionStateStopped,
	SessionStatePaused,
	nSessionStates
};

enum {
	SessionStateRequestNone = 0,
	SessionStateRequestNew,
	SessionStateRequestResume,
	SessionStateRequestStop,
	SessionStateRequestPause,
	nSessionStateRequests
};

volatile int session_state = SessionStateIdle;
volatile int session_state_change_request = SessionStateRequestNone;
static std::string session_vfile;
int exposure_setting = 0;

using namespace cv;
using namespace std;

/* default settings */
#define DEFAULT_SLEEP_WAIT 10000

enum {
	SelfThreadId = 0,
	ServerThreadId,
	NThreadIds
};
pthread_t *tid = 0;

/* function prototypes */

void *server_loop(void *ptr);
bool compress_frame(Camera *camera, void *ibuff, std::vector<uchar> &obuff);
bool thumbnail(void *ibuff, RegionOfInterest *roi, int input_data_format, const char *filename);

/* local variables and functions */

bool read_m021_register_callback(ReadWriteRegisterArgs *args, void *ext) {
	unsigned int val = *args->p;
	printf("read_m021_register(0x%x) returns 0x%x = %d\n", args->reg, val, val);
	return true;
}

bool write_m021_register_callback(ReadWriteRegisterArgs *args, void *ext) {
	printf("write_m021_register(0x%x, 0x%x=%d)\n", args->reg, args->val, args->val);
	return true;
}

#define GAMMA_LUT_SIZE 4096
int gamma_lut_size = GAMMA_LUT_SIZE;
unsigned char gamma_lut[GAMMA_LUT_SIZE]; /* 12 bits LUT for gamma correction */

unsigned int width = 1280, height = 960, frame_index = 0, session_frame_index = 0;
volatile unsigned int request_image_compression = 0;
std::vector<uchar> compressed_image(1280 * 960 * 4); /* allocate huge size to avoid dynamic reallocation */

bool debug = false, run = true;

/* autoexposure */

typedef struct {
	int threshold, n_over_threshold;
	double sumx0, sumx1, sumx2, mean, rms, fraction_over_threshold;
} Statistics;

int ae_phase = 0, ae_n_phases = 1;
bool finalize_autoexposure();
// bool initialize_autoexposure();
int calculate_exposure(int current_exposure, float target_mean, Statistics *stats);
int calculate_exposure(int current_exposure, float target_mean, float current_mean);
// bool analyze_autoexposure_roi(RegionOfInterest *roi, unsigned short int *frame_buffer, Statistics *stats, int nchans, bool hist = false);

/***/

/* when we hit ^C tell main loop to exit */
static void stop(int sig) {
	syslog(LOG_INFO, "nauto_main: %d signal received", sig);
	run = false;
}

/* PID file creation */
static pid_t myPid = 0;
static char pidPathPrefix[64]; 
static char pidPathFull[64];
static int createPidFile(int deviceNumber) {
	char strBuffer[64];
	FILE *pidFD = NULL;

	/* Create pid file */
	snprintf(pidPathPrefix, sizeof(pidPathPrefix), WHO_AM_I);
	memset(strBuffer, 0, sizeof(strBuffer));
	snprintf(strBuffer, sizeof(strBuffer), "%s%d.pid", pidPathPrefix, deviceNumber);
	pidFD = fopen(strBuffer, "w+");
	if(NULL == pidFD) {
		syslog(LOG_ERROR, "Can't open %s", strBuffer);
		return -1;
	}

	myPid = getpid();
	fprintf(pidFD, "%d", myPid);
	fclose(pidFD);

	strncpy(pidPathFull, strBuffer, sizeof(pidPathFull));
	syslog(LOG_INFO, "Created pid file %s for device %d", pidPathFull, deviceNumber);
	return 0;
}

/* FW Version */
static char version[] = FWVERSION;
static char githash[] = GITHASH;

/*** begin camera ***/

bool display_fxn(Camera *camera, unsigned char *data_ptr, void *ext) {
	DisplayParams *params = (DisplayParams *)ext;
	int i, j, rows = camera->get_capture_rows(), cols = camera->get_capture_cols(), type = camera->get_type();

	Mat frame;

	if(type == Camera::CAMERA_LI_USB3_MT9M021C) { 
printf("display_fxn: WxH = %dx%d\n", params->roi->cols, params->roi->rows);
		frame = Mat(params->roi->rows, params->roi->cols, CV_8UC3, (void *)data_ptr);
	} else if(type == Camera::CAMERA_LI_USB3_MT9M021M) { 
		frame = Mat(rows, cols, CV_16UC1, (void *)data_ptr);
	}
	imshow(params->window_name, frame);

	int key = cvWaitKey(10);

	return true;

#if 0
	unsigned short int *pi = (unsigned short int *)data_ptr;
	unsigned char *po = params->work_buffer;
	for(i=0;i<height;++i) {
		for(j=0;j<width;++j) {
			int index = *pi++;
			index >>= 4;
			*po++ = params->gamma_lut[index];
		}
	}
#else

	RegionOfInterest roi;
	roi.start_col = 0;
	roi.end_col = width - 1;
	roi.start_row = height / 4;
	roi.end_row = 3 * height / 4 - 1;
	roi.rows = roi.end_row - roi.start_row + 1;
	roi.cols = roi.end_col - roi.start_col + 1;
	int end_col = roi.end_col;
	int end_row = roi.end_row;
	unsigned short int *pi = (unsigned short int *)data_ptr;
	pi += width * roi.start_row; 
	unsigned char *po = params->work_buffer;
	for(i=roi.start_row;i<=end_row;++i) {
		for(j=roi.start_col;j<=end_col;++j) {
			int index = *pi++;
			index >>= 4;
			*po++ = params->gamma_lut[index];
		}
	}

#endif

}

void make_gamma_lut(float gamma, unsigned char *lut, int size) {
	float norm = 256.0 / pow(size, 1.0 / gamma);
	for(int i=0;i<size;++i) {
		float x = i;
		float y = i ? pow(x, 1.0 / gamma) * norm: 0.0;
		lut[i] = (y < 255.0) ? floor(y + 0.5) : 255;
	}
}

/*** end camera ***/

int main(int argc, char **argv) {

	tid = new pthread_t [ NThreadIds ];

	uint64_t vstart;
	char str[1024], logbuff[1024];
	int i, err, nframes = 0, ncompress = 0, device = -1, compression_scheme = Camera::COMPRESSION_NONE, compression_quality = 0;
	std::string cfile, vfile0, ffile0 = "frame.dat", tfile0, tfile, vfile, ffile, fourcc_string = "";
	std::string gps_host = "localhost", imu_host = "localhost";
	int duration = 0, logbuff_length = sizeof(logbuff), exposure = 0;
	int server_port = -1, skip = 0, delta_thumbnail_time = 1000;
	bool calibrate = false, display = false, verbose = false, do_video = false, do_server = false, do_thumbnails = false;
	bool autoexposure = false, xu4 = false, dry_run = false, forward = false, rear = false, use_default_roi = true, enable_hw_ae = false;
	Mat camera_matrix, dist_coeffs;
	float gamma = 1.0, target_mean = 0.375;
	double fps = 30.0;
	char *thread_result;
	FILE *vfp = 0, *ffp = 0;
	VideoWriter *video_writer = 0; 

/* jsv need to implement */
	int n_output_buffers = 3, n_input_buffers = 6;
	int camera_type = -1;

	RegionOfInterest capture_roi, exposure_roi;
	memset(&capture_roi, 0, sizeof(RegionOfInterest));
	memset(&exposure_roi, 0, sizeof(RegionOfInterest));

	for(i=1;i<argc;++i) {
		syslog(LOG_NOTICE, "%d: %s", i, argv[i]);
		if(strcmp(argv[i], "-debug") == 0) debug = true;
		else if(strcmp(argv[i], "-verbose") == 0) verbose = true;
		else if(strcmp(argv[i], "-server") == 0) {
			do_server = true;
			server_port = atoi(argv[++i]);
		}
		else if(strcmp(argv[i], "-gamma") == 0) gamma = atof(argv[++i]);
		else if(strcmp(argv[i], "-exposure") == 0) exposure = atoi(argv[++i]);
		else if(strcmp(argv[i], "-autoexposure") == 0) autoexposure = true; 
		else if(strcmp(argv[i], "-enable_hw_ae") == 0) enable_hw_ae = true; 
		else if(strcmp(argv[i], "-mean") == 0) target_mean = atof(argv[++i]); 
		else if(strcmp(argv[i], "-device") == 0) device = atoi(argv[++i]);
		else if(strcmp(argv[i], "-d") == 0) { do_video = true; device = atoi(argv[++i]); }
		else if(strcmp(argv[i], "-height") == 0) height = atoi(argv[++i]);
		else if(strcmp(argv[i], "-width") == 0) width = atoi(argv[++i]);
		else if(strcmp(argv[i], "-skip") == 0) skip = atoi(argv[++i]);
		else if(strcmp(argv[i], "-duration") == 0) duration = atoi(argv[++i]);
		else if(strcmp(argv[i], "-c") == 0) cfile = argv[++i];
		else if(strcmp(argv[i], "-calibrate") == 0) cfile = argv[++i];
		else if(strcmp(argv[i], "-fourcc") == 0) fourcc_string = argv[++i];
		else if(strcmp(argv[i], "-o") == 0) vfile0 = argv[++i];
		else if(strcmp(argv[i], "-video") == 0) vfile0 = argv[++i];
		else if(strcmp(argv[i], "-thumbnails") == 0) {
			do_thumbnails = true;
			tfile0 = argv[++i];
			delta_thumbnail_time = atoi(argv[++i]);
		} else if(strcmp(argv[i], "-video_output") == 0) vfile = argv[++i];
		else if(strcmp(argv[i], "-xu4") == 0) xu4 = true; 
		else if(strcmp(argv[i], "-display") == 0) display = true; 
		else if(strcmp(argv[i], "-output_buffers") == 0) n_output_buffers = atoi(argv[++i]);
		else if(strcmp(argv[i], "-input_buffers") == 0) n_input_buffers = atoi(argv[++i]);
		else if(strcmp(argv[i], "-fps") == 0) fps = atof(argv[++i]);
		else if(strcmp(argv[i], "-mjpg") == 0) fourcc_string = "MJPG"; 
		else if(strcmp(argv[i], "-mp4") == 0) fourcc_string = "FMP4"; 
		else if(strcmp(argv[i], "-m021m") == 0) camera_type = Camera::CAMERA_LI_USB3_MT9M021M;
		else if(strcmp(argv[i], "-m021c") == 0) camera_type = Camera::CAMERA_LI_USB3_MT9M021C;
		else if(strcmp(argv[i], "-dry_run") == 0) dry_run = true; 
		else if(strcmp(argv[i], "-forward") == 0) {
			forward = true;  
			use_default_roi = false;
		} else if(strcmp(argv[i], "-rear") == 0) {
			rear = true; 
			use_default_roi = false;
		} else if(strcmp(argv[i], "-roi") == 0) {
			capture_roi.start_col = atoi(argv[++i]);
			capture_roi.start_row = atoi(argv[++i]);
			capture_roi.end_col = atoi(argv[++i]);
			capture_roi.end_row = atoi(argv[++i]);
			use_default_roi = false;
		} else if(strcmp(argv[i], "-vga") == 0) {
			height = 480;
			width = 640;
		} else if(strcmp(argv[i], "-vga2") == 0) {
			height = 960;
			width = 1280;
		} else if(strcmp(argv[i], "-720p") == 0) {
			height = 720;
			width = 1280;
		} else if(strcmp(argv[i], "-800p") == 0) {
			height = 800;
			width = 1280;
		} else if(strcmp(argv[i], "-960p") == 0) {
			height = 960;
			width = 1280;
		} else if(strcmp(argv[i], "-jpg") == 0) {
			compression_scheme = Camera::COMPRESSION_JPEG;
			compression_quality = atoi(argv[++i]);
		} else {
			snprintf(logbuff, logbuff_length, "unrecognized argument [%s]", argv[i]);
			log_fxn(LEVEL_ERROR, logbuff);
			return -1;
		}
	}

	char tstr[4];
	snprintf(tstr, sizeof(tstr), "%d", device);
	size_t tpos = ffile0.find_last_of('.');
	std::string tmp_suffix, tmp_file;
	if(tpos != std::string::npos) {
		tmp_suffix = ffile0.substr(tpos); /* includes the "." */
		tmp_file = ffile0.substr(0, tpos);
	} else {
		tmp_file = ffile0;
	}
	ffile0 = tmp_file;
	ffile0 += tstr;
	ffile0 += tmp_suffix;

/* jsv */
	fps /= (skip + 1);

	LogInfo log_info;
	log_info.buff = logbuff;
	log_info.buff_length = logbuff_length;
	log_info.fxn = log_fxn;

/* for identifying one's self */
	char hostname[256];
	gethostname(hostname, sizeof(hostname)-1);
	snprintf(logbuff, logbuff_length, "hostname = %s", hostname);
	log_fxn(LEVEL_INFO, logbuff);

	pthread_t thread;
	cpu_set_t cpu_set;
	unsigned int cpu_mask, camera_incoming_thread_cpu_mask = 0, camera_outgoing_thread_cpu_mask = 0, 
		server_thread_cpu_mask = 0, camera_interface_thread_cpu_mask = 0, main_cpu_mask = 0;
	if(xu4) {
		cpu_mask = XU4_LARGE_CORE_0 | XU4_LARGE_CORE_1 | XU4_LARGE_CORE_2 | XU4_LARGE_CORE_3;
		main_cpu_mask = cpu_mask;
		camera_incoming_thread_cpu_mask = cpu_mask;
		camera_outgoing_thread_cpu_mask = cpu_mask;
		cpu_mask = XU4_SMALL_CORE_0 | XU4_SMALL_CORE_1 | XU4_SMALL_CORE_2 | XU4_SMALL_CORE_3;
		server_thread_cpu_mask = cpu_mask;
		camera_interface_thread_cpu_mask = cpu_mask;
	}

	thread = pthread_self();
	set_affinity(&thread, main_cpu_mask, "main loop", &log_info);

/* start syslog */
	char logName[15];
	snprintf(logName, sizeof(logName), "nauto_main%d", device);
	openlog(logName, LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "%s started", logName);

        createPidFile(device);
	signal(SIGINT, stop); /* ^C  exception handling */ 
	signal(SIGTERM, stop); /* exception handling */ 

	if(do_server) {
		server_params.port = server_port;
		server_params.display_params = display_params;
		server_params.camera = camera;
		err = pthread_create(&tid[ServerThreadId], NULL, &server_loop, (void *)&server_params);
		set_affinity(&tid[ServerThreadId], server_thread_cpu_mask, "mini-server", &log_info);
		wait_for_thread_to_start(&server_params.thread_started, "mini-server", &log_info);
	}

	unsigned char *tmp_buff = new unsigned char[ height * width ]; /* for downshifting image */

	if(exposure > 0) { /* set on command line */
		camera->write_m021_register(M021_COARSE_INTEGRATION_TIME_REGISTER, exposure); 
		printf("exposure set to %d\n", exposure);
	}

	while(run == true) {

		now = get_time_ms(); /* good time to update *now* */

		void *incoming_buffer = 0;
		bool wait_ready = true;
		if(incoming_buffer = camera->get_incoming_buffer(wait_ready)) {

			if(request_image_compression) {
				compress_frame(camera, (unsigned char *)incoming_buffer, compressed_image);
				request_image_compression = 0;
			}

			if(video_writer && ((frame_index % (skip + 1)) == 0)) { /* skip frames? */

				int start_col = 0; 
				int start_row = 0; 
				int end_col = capture_roi.cols - 1;
				int end_row = capture_roi.rows - 1;
				int cols = capture_roi.cols;
				int rows = capture_roi.rows;

				if(camera->get_type() == Camera::CAMERA_LI_USB3_MT9M021M) { 

					int nchans = 1;
					unsigned short int *xp = (unsigned short int *)incoming_buffer;
					xp += start_row * cols;
					unsigned char *pd = tmp_buff;
					for(int i=start_row;i<=end_row;++i) {
						for(int j=start_col;j<=end_col;++j) {
							unsigned short int xi = *xp++; 
							*pd++ = gamma_lut[xi >> 4];
						}
					}
					Mat frame(rows, cols, CV_8UC1, (void *)tmp_buff);
// printf("frame(%d, %d, %d, %p)\n", height, width, CV_8UC1, tmp_buff);
					if(dry_run == false) video_writer->write(frame);
					++ncompress;

				} else if(camera->get_type() == Camera::CAMERA_LI_USB3_MT9M021C) { 

					int chan, nchans = 3;
					unsigned char *data_ptr = (unsigned char *)incoming_buffer;
					Mat frame(rows, cols, CV_8UC3, (void *)data_ptr);
// printf("frame(%d, %d, %d, %p)\n", height, width, CV_8UC3, data_ptr);
					if(dry_run == false) video_writer->write(frame);
					++ncompress;
				}

				fprintf(ffp, "%"PRIu64" %d\n", 
					camera->get_incoming_buffer_timestamp(), camera->get_incoming_buffer_index());

			}
			++frame_index;

if(enable_hw_ae && ((frame_index % 60) == 0)) {
	// camera->read_m021_register(M021_REGISTER, &m021_registers., &read_register_callback_params);
	// camera->read_m021_register(M021_AE_CTRL_REGISTER, &m021_registers.ae_ctrl, &read_register_callback_params);
	camera->read_m021_register(M021_COARSE_INTEGRATION_TIME_REGISTER, &m021_registers.coarse_integration_time, &read_register_callback_params);
	camera->read_m021_register(M021_FINE_INTEGRATION_TIME_REGISTER, &m021_registers.fine_integration_time, &read_register_callback_params);
	camera->read_m021_register(M021_AE_MEAN_I_REGISTER, &m021_registers.ae_mean_luma, &read_register_callback_params);
	camera->read_m021_register(M021_AE_TARGET_LUMA_REGISTER, &m021_registers.ae_target_luma, &read_register_callback_params);
	camera->read_m021_register(M021_AE_COARSE_INTEGRATION_TIME_REGISTER, &m021_registers.ae_coarse_integration_time, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_ROI_X_START_REGISTER, &m021_registers.ae_roi_x_start, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_ROI_Y_START_REGISTER, &m021_registers.ae_roi_y_start, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_ROI_X_SIZE_REGISTER, &m021_registers.ae_roi_x_size, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_ROI_Y_SIZE_REGISTER, &m021_registers.ae_roi_y_size, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_AG_EXPOSURE_HI_REGISTER, &m021_registers.ae_ag_exposure_hi, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_AG_EXPOSURE_LO_REGISTER, &m021_registers.ae_ag_exposure_lo, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_MIN_EV_STEP_REGISTER, &m021_registers.ae_min_ev_step, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_MAX_EV_STEP_REGISTER, &m021_registers.ae_max_ev_step, &read_register_callback_params);
	// camera->read_m021_register(M021_EMBEDDED_DATA_CTRL_REGISTER, &m021_registers.embedded_data_ctrl, &read_register_callback_params);
	// camera->read_m021_register(M021_AE_DARK_CURRENT_THRESHOLD_REGISTER, &m021_registers.ae_dark_current_threshold, &read_register_callback_params);
}

#if 1
			set_exposure_countdown--;
			if(autoexposure && !enable_hw_ae && (set_exposure_countdown <= 0)) {
				int nchans = is_color ? 3 : 1; 
				set_exposure_countdown = 0; 
				float current_mean = camera->get_capture_roi_mean_intensity();
printf("mean intensity = %f\n", current_mean);
				int new_exposure_setting = calculate_exposure(exposure_setting, target_mean, current_mean);
				if(new_exposure_setting != exposure_setting) {
printf("new exposure = %d. mean = %f/%f. frame index = %d\n", exposure_setting, stats.mean, target_mean, frame_index);
					exposure_setting = new_exposure_setting;
					camera->write_m021_register(M021_EXPOSURE_REGISTER, exposure_setting);
				/* proper amount of time for new exposure settings to propagate through pipeline
					add an additional 3 frames to account for thread which writes the setting */
					set_exposure_countdown = n_capture_buffers + n_incoming_buffers + 2 + 3;
printf("new exposure countdown = %d = %d + %d + 2 + 3\n", set_exposure_countdown, n_capture_buffers, n_incoming_buffers);
				}
			}
#endif

			if(do_thumbnails && (now > thumbnail_timeout)) {
				std::string suffix = get_suffix(tfile0);
				std::string dir = dirname(tfile0);
				tfile = basename(tfile0, true);
				tfile = dir + "/" + tfile + "_"; /* remove suffix */
				char tstr[32];
				snprintf(tstr, sizeof(tstr), "%"PRIu64".", now);
				tfile += tstr;
				tfile += suffix;
printf("thumbnail = [%s]\n", tfile.c_str());
				thumbnail_timeout += delta_thumbnail_time;
				int input_data_format = -1;
				if(camera_type == Camera::CAMERA_LI_USB3_MT9M021M) 
					input_data_format = VideoCompression::FRAME_DATA_FORMAT_16UC1;
				else if(camera_type == Camera::CAMERA_LI_USB3_MT9M021C) 
					input_data_format = VideoCompression::FRAME_DATA_FORMAT_8UC3;
				thumbnail(incoming_buffer, &thumbnail_roi, input_data_format, tfile.c_str());
			}

// printf("frame %d\n", frame_index);
			camera->release_incoming_buffer();

		/* periodically log frame rate */
			++nframes;
			int dseconds = 10; /* how often to report */
			now = get_time_ms(); /* probably a good time to update *now* */
			if(nframes && ((nframes % (30 * dseconds)) == 0)) {
				uint64_t dtime = now - prev_time;
				prev_time = now;
				int dframes = nframes - prev_frames;
				prev_frames = nframes;
				snprintf(logbuff, logbuff_length, 
					"%d / %d frames compressed. fps = %"PRIu64"", 
					ncompress, nframes, 1000 * dframes / dtime);
				log_fxn(LEVEL_INFO, logbuff);
			}

		} else if(incoming_buffer == 0) { /* had timeout  */

			if((device_status = camera->get_device_status()) == false) {
				camera->close();
				delete camera;
				camera = 0;
				session_state = SessionStateStopped;
				run = false;
			}
	
		}

	/* session states can cascade into each other */
	/* this section processes requests for changes in session state, including a new session */
		if(session_state_change_request == SessionStateRequestNew) {
			int session_vfile_length = session_vfile.length(); /* session_odir must be properly set */
			if(session_state == SessionStateIdle && session_vfile_length > 0) {
				snprintf(logbuff, logbuff_length, "new session request with output [%s]", 
					session_vfile.c_str()); 
				log_fxn(LEVEL_INFO, logbuff);
				session_state = SessionStatePending; /* going for a new session */
				session_state_change_request = SessionStateRequestNone;
			} else if(session_state == SessionStateRunning) {
				snprintf(logbuff, logbuff_length, "session close requested"); 
				log_fxn(LEVEL_INFO, logbuff);
				session_state = SessionStateStopped;
			}
		} else if(session_state_change_request == SessionStateRequestStop) {
			snprintf(logbuff, logbuff_length, "session pause requested"); 
			log_fxn(LEVEL_INFO, logbuff);
			session_state = SessionStateStopped;
			session_state_change_request = SessionStateRequestNone;
		} else if(session_state_change_request == SessionStateRequestPause) {
			snprintf(logbuff, logbuff_length, "session pause requested"); 
			log_fxn(LEVEL_INFO, logbuff);
			session_state = SessionStatePaused;
			session_state_change_request = SessionStateRequestNone;
		} else if(session_state_change_request == SessionStateRequestResume) {
			snprintf(logbuff, logbuff_length, "session resume requested"); 
			log_fxn(LEVEL_INFO, logbuff);
			session_state = SessionStateRunning;
			session_state_change_request = SessionStateRequestNone;
		}

		if(session_state == SessionStatePaused) {
			usleep(1000000); /* sleep for a sec */
		}

		if(session_state == SessionStateIdle) {
			if(session_vfile.length()) session_state = SessionStatePending;
		}

		if(session_state == SessionStatePending) { /* begin a new session */

			now = get_time_ms();

			snprintf(logbuff, logbuff_length, "new session at %"PRIu64" into output [%s]", now, session_vfile.c_str()); 
			log_fxn(LEVEL_INFO, logbuff);

			session = new Session(vfile0, duration);
			session->log_fxn = log_fxn;

			session->new_run();

			ffile = session->next_file(ffile0);
			ffp = fopen(ffile.c_str(), "w");
			vfile = session->next_file(vfile0);
			video_writer = new VideoWriter(vfile.c_str(), fourcc, fps, cvSize(capture_roi.cols, capture_roi.rows), is_color);
			vstart = session->timestamp;

			session_frame_index = 0;
			session_vfile = "";
			session_state = SessionStateRunning;
		}

		if(session_state == SessionStateRunning) {

			if(session->run_expire()) {
				if(video_writer) {
					delete video_writer; /* closes current video writer */
					now = get_time_ms();
					uint64_t vduration = vstart - now; 
					int size = get_filesize(vfile.c_str()); 
					snprintf(logbuff, logbuff_length, 
						"closing previous video file [%s]. start time = %"PRIu64". duration = %"PRIu64". file size = %d", 
						vfile.c_str(), vstart, vduration, size); 
					log_fxn(LEVEL_INFO, logbuff);
					session->stage_to_upload(vfile); /* remove *working* status */
					if(ffp) fclose(ffp);
					session->stage_to_upload(ffile); /* remove *working* status */
				}

				session->new_run();
				ffile = session->next_file(ffile0);
				ffp = fopen(ffile.c_str(), "w");
				vstart = session->timestamp;
				vfile = session->next_file(vfile0);
			/* make new video writer as soon as possible to reduce possibility of frame loss */
				video_writer = new VideoWriter(vfile.c_str(), fourcc, fps, cvSize(capture_roi.cols, capture_roi.rows), is_color);
				snprintf(logbuff, logbuff_length, "VideoWriter(%s, 0x%x, %f, %dx%d, %s)",
					vfile.c_str(), fourcc, fps, capture_roi.cols, capture_roi.rows, is_color ? "true" : "false");
				log_fxn(LEVEL_INFO, logbuff);
				if(video_writer->isOpened() == false) {
					snprintf(logbuff, logbuff_length, "error opening video file [%s]", vfile.c_str());
					log_fxn(LEVEL_ERROR, logbuff);
				}
			}

		}

		if(session_state == SessionStateStopped) {

			if(video_writer) delete video_writer;
			now = get_time_ms(); 
			uint64_t vduration = vstart - now; 
			int size = get_filesize(vfile.c_str()); 
			snprintf(logbuff, logbuff_length, 
				"closing previous video file [%s]. start time = %"PRIu64". duration = %"PRIu64". file size = %d", 
				vfile.c_str(), vstart, vduration, size); 
			log_fxn(LEVEL_INFO, logbuff);
			if(session) session->stage_to_upload(vfile); /* remove *working* status */
			if(ffp) fclose(ffp);
			if(session) session->stage_to_upload(ffile); /* remove *working* status */

			uint64_t dtime = now - begin_time; 
			double fps = (double)nframes;
			fps = fps / dtime;
			snprintf(logbuff, logbuff_length, "%f frames per second", fps);
			log_fxn(LEVEL_INFO, logbuff);

			if(session) delete session;
			session = 0;
			session_state = SessionStateIdle;

		}

	}

	if(session) {

		if(video_writer) delete video_writer;
		now = get_time_ms(); 
		uint64_t vduration = vstart - now; 
		int size = get_filesize(vfile.c_str()); 
		snprintf(logbuff, logbuff_length, 
			"closing previous video file [%s]. start time = %"PRIu64". duration = %"PRIu64". file size = %d", 
			vfile.c_str(), vstart, vduration, size); 
		log_fxn(LEVEL_INFO, logbuff);
		session->stage_to_upload(vfile); /* remove *working* status */
		if(ffp) fclose(ffp);
		session->stage_to_upload(ffile); /* remove *working* status */

		delete session;
		session = 0;
		session_state = SessionStateIdle;

	}

	if(camera && (camera->stop_capture() == false)) {
		snprintf(logbuff, logbuff_length, "ERROR: unable to gracefully shutdown camera");
		log_fxn(LEVEL_ERROR, logbuff);
	}

	if(camera) {
		camera->close();
		delete camera;
	}

	delete [] tmp_buff;
	if(display_params->work_buffer) delete [] display_params->work_buffer;

	if(do_server) {
		snprintf(logbuff, logbuff_length, "shutting down server service");
		log_fxn(LEVEL_INFO, logbuff);
		server_params.run = 0;
		pthread_join(tid[ServerThreadId], (void**) &thread_result);
		snprintf(logbuff, logbuff_length, "server service stopped");
		log_fxn(LEVEL_INFO, logbuff);
	}

/* remove pid file */
	if(remove(pidPathFull)) syslog(LOG_ERR, "%s cannot be removed", pidPathFull);
	else syslog(LOG_NOTICE, "Removing %s", pidPathFull);
        syslog(LOG_INFO, "Exiting nauto_main");
        closelog();

	delete [] tid;

	return device_status ? 0 : -1;
}

unsigned char thumbnail_buffer [ 3 * 1280 * 960 ];
bool thumbnail(void *ibuff, RegionOfInterest *roi, int input_data_format, const char *filename) {

	int chan, chans = 0, cv_code;
	if(input_data_format == VideoCompression::FRAME_DATA_FORMAT_16UC1) { chans = 1; cv_code = CV_16UC1; }
	else if(input_data_format == VideoCompression::FRAME_DATA_FORMAT_8UC3) { chans = 3; cv_code = CV_8UC3; }
	if(chans == 0) return false; /* unrecognized format */
	int row, rows = roi->rows, end_row = roi->end_row, skip_rows = roi->skip_rows; 
	int col, cols = roi->cols, end_col = roi->end_col, skip_cols = roi->skip_cols;
	int size = 4 * chans * rows * cols / roi->skip_rows / roi->skip_cols; /* max data size is 32-bits per pixel */
	unsigned char *obuff = thumbnail_buffer; 
	if(size > sizeof(thumbnail_buffer)) return false;
	if((skip_cols == 0) || (skip_rows == 0)) return false;

/* scale down image. the code bodies below are algorithmically identical except for the data types.
	in principle, they could be unified, but the code would be inefficient.
	Also, OpenCV needs 8 bit data for compression. 16-bits won't work */
	if(input_data_format == VideoCompression::FRAME_DATA_FORMAT_8UC3) {
		cv_code = CV_8UC3;
		unsigned char *optr = (unsigned char *)obuff;
		for(row=roi->start_row;row<=end_row;row+=skip_rows) {
			unsigned char *iptr = (unsigned char *)ibuff;
			iptr += (row * cols + roi->start_col) * chans;
			for(col=roi->start_col;col<=end_col;col+=skip_cols) {
				for(chan=0;chan<chans;++chan) *optr++ = iptr[chan];
				iptr += chans * skip_cols;
			}
		}
	} else if(input_data_format == VideoCompression::FRAME_DATA_FORMAT_16UC1) { 
		cv_code = CV_8UC1;
		unsigned char *optr = (unsigned char *)obuff;
		unsigned short int mask = gamma_lut_size - 1;
		for(row=roi->start_row;row<=end_row;row+=skip_rows) {
			unsigned short int *iptr = (unsigned short int *)ibuff;
			iptr += (row * cols + roi->start_col) * chans;
			for(col=roi->start_col;col<=end_col;col+=skip_cols) {
				unsigned short int x = iptr[0];
				*optr++ = gamma_lut[(x >> 4) & mask];
				iptr += chans * skip_cols;
			}
		}
	}

/* prepare for compression */
	std::vector<int> compression_params = std::vector<int>(2);
	int compression_quality = 95;
	compression_params[0] = CV_IMWRITE_JPEG_QUALITY;
	compression_params[1] = compression_quality;
	
	int height = roi->rows / skip_rows, width = roi->cols / skip_cols;

	Mat frame(height, width, cv_code, (void *)obuff);

	imwrite(filename, frame, compression_params);

	return true;
}

/* note: one should preallocate a large vector for obuff so that resizing won't happen */
bool compress_frame(Camera *camera, void *ibuff, std::vector<uchar> &obuff) {
printf("compress_frame(camera=%p, ibuff=%p, obuff=%p)\n", camera, ibuff, &obuff);
	int height = camera->get_height(), width = camera->get_width(), type = camera->get_type();
	std::vector<int> compression_params = std::vector<int>(2);
	int compression_quality = 95;
	compression_params[0] = CV_IMWRITE_JPEG_QUALITY;
	compression_params[1] = compression_quality;
	obuff.clear();
	if(type == Camera::CAMERA_LI_USB3_MT9M021M) {
/* jsv. i am not proud of this. I have to take a perfectly good grey scale 16-bit image 
   and scale down to 8-bits for the compression to work */
/* instead of this: Mat src_frame(height, width, CV_16UC1, (void *)ibuff);
 * i have to do the following: */
		// jsv was unsigned char *tbuff = new unsigned char[ height * width * 3];
		unsigned char *tbuff = new unsigned char[ height * width ];
		unsigned short int *xp = (unsigned short int *)ibuff;
		for(int i=0;i<height;++i) {
			for(int j=0;j<width;++j) {
				unsigned short int xi = *xp++; 
				unsigned char x = (xi >> 8);
				tbuff[i * width + j] = x;
			}
		}
		Mat src_frame(height, width, CV_8UC1, (void *)tbuff);
		imencode(".jpg", src_frame, obuff, compression_params);
printf("MT9M021M: compressed size = %d\n", obuff.size());
		delete [] tbuff;
	} else if(type == Camera::CAMERA_LI_USB3_MT9M021C) {
/* TODO jsv rework for 8-bit or 16-bit */
/* jsv. i am not proud of this. I have to take a perfectly good 16-bit BGR image 
   and scale down to 8-bits for the compression to work */
/* instead of this: Mat src_frame(height, width, CV_16UC3, (void *)ibuff);
 * i have to do the following: */
		int nchans = 3, rows = height / 2, cols = width / 2;
		unsigned char *tbuff = new unsigned char[ rows * cols * 3 ];
		unsigned short int *xp = (unsigned short int *)ibuff;
		for(int i=0;i<rows;++i) {
			for(int j=0;j<cols;++j) {
				unsigned short int bi = *xp++; 
				unsigned char b = (bi >> 8);
				unsigned short int gi = *xp++; 
				unsigned char g = (gi >> 8);
				unsigned short int ri = *xp++; 
				unsigned char r = (ri >> 8);
				tbuff[nchans * (i * width + j) + 0] = b;
				tbuff[nchans * (i * width + j) + 1] = g;
				tbuff[nchans * (i * width + j) + 2] = r;
			}
		}
		Mat src_frame(height, width, CV_8UC3, (void *)tbuff);
		imencode(".jpg", src_frame, obuff, compression_params);
printf("MT9M021C: compressed size = %d\n", obuff.size());
		delete [] tbuff;
	}
	return true;
}

/*** begin autoexposure ***/

/* NBINS needs to be a power of 2 */
// #define NBINS 256
// int n_bins = 0;
// int *pixel_histogram = 0;
float  min_exposure = 1.0, max_exposure = 2000.0;
float damp_offset = 0.0;
float damp_gain = 0.8;

int calculate_exposure(int current_exposure, float target_mean, float current_mean) {
	float target_ratio = target_mean / current_mean;
	float new_exp = log2(target_ratio);
	float recursive_damp = damp_offset + abs(new_exp) * damp_gain;
	float new_exp_damped = recursive_damp * new_exp;
	float new_exp_ratio = pow(2.0, new_exp_damped);
	float exposure = current_exposure * new_exp_ratio;
	if(exposure < min_exposure) exposure = min_exposure;
	else if(exposure > max_exposure) exposure = max_exposure;
	else if(isnan(exposure)) exposure = 100; /* got a NaN or something else */
// printf("calculate_exposure() => %5.3f target ratio = %5.3f mean = %5.3f target = %5.3f \n", 
// exposure, target_ratio, stats->mean, target_mean);
	return (int)exposure;
}

int calculate_exposure(int current_exposure, float target_mean, Statistics *stats) {
	float target_ratio = target_mean / stats->mean;
	float new_exp = log2(target_ratio);
	float recursive_damp = damp_offset + abs(new_exp) * damp_gain;
	float new_exp_damped = recursive_damp * new_exp;
	float new_exp_ratio = pow(2.0, new_exp_damped);
	float exposure = current_exposure * new_exp_ratio;
	if(exposure < min_exposure) exposure = min_exposure;
	else if(exposure > max_exposure) exposure = max_exposure;
	else if(isnan(exposure)) exposure = 100; /* got a NaN or something else */
// printf("calculate_exposure() => %5.3f target ratio = %5.3f mean = %5.3f target = %5.3f \n", 
// exposure, target_ratio, stats->mean, target_mean);
	return (int)exposure;
}

#if 0
bool initialize_autoexposure() {
	n_bins = NBINS;
	pixel_histogram = new int [ n_bins ];
	ae_phase = 0;
	ae_n_phases = 1;
}

bool finalize_autoexposure() {
	delete [] pixel_histogram;
}
#endif

#if 0
bool analyze_autoexposure_roi(RegionOfInterest *roi, unsigned short int *frame_buffer, Statistics *stats, int nchans, bool hist) {
	int i, row, col, end_row = roi->end_row, end_col = roi->end_col, rows = roi->rows, cols = roi->cols;
	double norm = 1.0, sumx0 = 0.0, sumx1 = 0.0, sumx2 = 0.0;
	int *histogram = pixel_histogram, mask = (n_bins - 1); /* n_bins needs to be a power of 2 */
	int index, threshold = stats->threshold, n_over_threshold = 0;
	bool rc = false; /* return code */
	// printf("analyze_autoexposure_roi(roi=%p, frame buffer=%p, stats=%p, nchans=%d, hist=%s)\n",
	// 	roi, frame_buffer, stats, nchans, hist ? "true" : "false");
	if(ae_phase == 0) {
		if(hist) memset(histogram, 0, n_bins * sizeof(int));
		stats->sumx0 = 0.0;
		stats->sumx1 = 0.0;
		stats->sumx2 = 0.0;
		stats->n_over_threshold = 0; 
	}
	if(nchans == 1) {
		norm = 1.0 / 65536.0; /* 16 bits */
		for(row=roi->start_row;row<=end_row;++row) {
			unsigned short int *src = (unsigned short int *)frame_buffer;
			col = roi->start_col;
			// src += (row * cols + col + ae_phase) * nchans;
			src += row * cols + col + ae_phase;
			// for(;col<end_col;col+=ae_n_phases,src+=(nchans * ae_n_phases)) {
			for(;col<end_col;col+=ae_n_phases,src+=ae_n_phases) {
				index = *src;
				if(hist) ++histogram[(index >> 8) & mask]; /* 16-bits down to 8-bits */
				sumx0 += 1.0;
				sumx1 += index;
				sumx2 += index * index;
				if(index >= threshold) n_over_threshold++;
			}
		}
	} else if(nchans == 3) {
		norm = 1.0 / 256.0; /* 8 bits */
		// printf("AE: ROWS(start/end) = %d/%d. phase=%d/%d\n", roi->start_row, end_row, ae_phase, ae_n_phases);
		for(row=roi->start_row;row<=end_row;++row) {
			unsigned char *src = (unsigned char *)frame_buffer;
			col = roi->start_col;
			src += (row * cols + col) * nchans;
			for(;col<end_col;col+=ae_n_phases,src+=(nchans * ae_n_phases)) {
				int b = src[0]; 
				int g = src[1];
				int r = src[2];
				double y = 0.299 * r + 0.587 * g + 0.114 * b; 
				int index = floor(y);
				if(hist) ++histogram[index & mask];
				if(index > threshold) n_over_threshold++;
// printf("bgr = %d/%d/%d Y=%f INDEX=%d. contribution=%f\n", b, g, r, y, index, index / 256.0);
// if(b || g || r) {
// printf("col=%d row=%d b=%d g=%d r=%d y=%f index=%d\n", col, row, b, g, r, y, index);
// getchar();
// }
				sumx0 += 1.0;
				sumx1 += y;
				sumx2 += y * y;
			}
		}
	}

	stats->n_over_threshold += n_over_threshold;
	stats->sumx0 += sumx0;
	stats->sumx1 += sumx1;
	stats->sumx2 += sumx2;

	++ae_phase;

	if(ae_phase >= ae_n_phases) {

		ae_phase = 0; /* reset the phase */

		int roi_size = (1 + end_row - roi->start_row) * (1 + end_col - roi->start_col);
		stats->fraction_over_threshold = (double)stats->n_over_threshold / (double)roi_size;
		double raw_mean = sumx1 / sumx0;
		stats->mean = norm * raw_mean;
// printf("raw mean = %f. normalized mean = %f\n", raw_mean, stats->mean);
		double arg = sumx2 / sumx0 - raw_mean * raw_mean;
		stats->rms = norm * sqrt(fabs(arg)); /* fabs() just to ensure for small neg numbers, no fault */
		rc = true;

	}

	return rc;
}

#endif

/*** end autoexposure ***/

#endif
