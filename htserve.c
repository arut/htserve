#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#define DEFAULT_PORT  8888
#define BACKLOG        100
#define MAX_RQ        1024
#define MAX_OUTBUF    1024

#define HTTP_OK        200
#define HTTP_NOT_FOUND 404

static int output_buf(int fd, const char *buf, size_t size) 
{
	int ret;

	for(;;) {
		ret = write(fd, buf, size);
		if (ret == size)
			return 0;
		if (ret == -1) {
			perror("write error");
			return 1;
		}
		buf += ret;
		size -= ret;
	}
}

static void* http_handler(void *arg) 
{
	int ss = (long)arg;
	char rq[MAX_RQ], *s = rq, *p = rq, *pp = p;
	size_t size;
	ssize_t ret;
	char *htrq = NULL, *uri;
	int fd = -1;
	int htcode, htmajor, htminor;
	char *htmsg, *htsig;
	char buf[MAX_OUTBUF];
	off_t offs;
	struct stat st;

	/* read HTTP request */
	size = sizeof(rq);
	for(;;) {

		ret = read(ss, s, size);
		
		if (!ret) {
			fprintf(stderr, "eof!\n");
			goto out;
		}

		if (ret < 0) {
			perror("read");
			goto out;
		}

		s += ret;
		size -= ret;

		for(;;) {

			pp = (char*)memchr(p, '\n', s - p);

			if (!pp)
				break;

			*pp = '\0';

			if (pp - p >= 1 && *(pp - 1) == '\r')
				*(pp - 1) = '\0';

			/* empty line ? */
			if (pp - p == 1
					|| pp - p == 2 && !*(pp - 1))
				goto done_rq;

			if (p == rq)
				htrq = p;
			
			/* else { process header fields } */

			p = pp;
			++p;
		}
	}

done_rq:

	/* parse HTTP request */
	if (!htrq) {
		fprintf(stderr, "empty HTTP request\n");
		goto out;
	}

	uri = strchr(htrq, ' ');
	if (uri == NULL) {
		fprintf(stderr, "malformed HTTP request\n");
		goto out;
	}
	*uri++ = 0;

	if (strcmp(htrq, "GET")) {
		fprintf(stderr, "unsupported method '%s'\n", htrq);
		goto out;
	}

	htsig = strrchr(uri, ' ');
	if (!htsig) {
		fprintf(stderr, "malformed HTTP request\n");
		goto out;
	}
	*htsig++ = 0;

	if (strncmp(htsig, "HTTP/1.", 7)
		|| !htsig[7] || htsig[7] != '0' && htsig[7] != '1')
	{
		fprintf(stderr, "malformed HTTP request finalizer\n");
		goto out;
	}
	htmajor = 1;
	htminor = htsig[7] - '0';

	/* output file */
	if (*uri == '/')
		++uri;

	fd = open(uri, O_RDONLY);

	if (fd != -1) {
		if (fstat(fd, &st)) {
			perror("fstat");
			goto out;
		}
	}

	if (fd == -1) {
		htcode = HTTP_NOT_FOUND;
		htmsg = "Not Found";

	} else {
		htcode = HTTP_OK;
		htmsg = "OK";
	}

	/* output HTTP status */
	size = snprintf(buf, sizeof(buf), 
			"HTTP/%d.%d %d %s\r\n"
			"Connection: Close\r\n%s",
			htmajor, htminor, htcode, htmsg,
			fd == -1 ? "\r\n" : "");

	if (output_buf(ss, buf, size))
		goto out;

	if (fd != -1) {

		/* output content-length */
		size = snprintf(buf, sizeof(buf),
				"Content-Length: %ld\r\n\r\n", st.st_size);

		if (output_buf(ss, buf, size))
			goto out;

		/* output body */
		for(offs = 0; offs != st.st_size; ) {
			if (sendfile(ss, fd, &offs, st.st_size - offs) == -1) {
				perror("sendfile");
				goto out;
			}
		}
	}

out:
	if (fd != -1)
		close(fd);

	close(ss);

	return NULL;
}

static const char shopts[] = "p:t:d:h";
static const struct option lopts[] = {
	{ "port",      1,  NULL,   'p' },
	{ "timeout",   1,  NULL,   't' },
	{ "directory", 1,  NULL,   'd' },
	{ "help",      0,  NULL,   'h' },
	{ NULL,        0,  NULL,    0  }
}; 
    
static void print_version()
{   
    printf("htserve - simple multithreaded HTTP "
			"server for static content\n") ;
} 

static inline void print_usage()
{
	print_version() ;
	printf("Usage: httpserve [OPTION*]\n"
			"  -p, --port       port to listen, default is %d\n"
			"  -t, --timeout    network IO timeout (in sec), default is infinity\n"
			"  -d, --directory  chdir to directory before runnung\n"
			"  -h, --help       display this help and exit\n\n"
			, DEFAULT_PORT);
	exit(0);
}

int main(int argc, char *argv[]) 
{
	int s, ss, opt, one = 1, port = DEFAULT_PORT;
	pthread_t pt;
	struct sockaddr_in sin = {
		.sin_family = AF_INET
	};
	struct timeval timeout = {
		.tv_sec = 0,
		.tv_usec = 0
	};

	while((opt = getopt_long(argc, argv, shopts, lopts, NULL)) != -1)
	{
		switch(opt) {
			case 'p':
				port = atoi(optarg);
				break;

			case 't':
				timeout.tv_sec = atoi(optarg);
				break;

			case 'd':
				if (chdir(optarg)) {
					perror("chdir");
					return 1;
				}
				break;

			case 'h':
				print_usage();
				break;

			default:
				return 1;
		}
	}

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1) {
		perror("socket");
		return 1;
	}

	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = INADDR_ANY;

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)))
		perror("reuseaddr");

	if (bind(s, (struct sockaddr*)&sin, sizeof(sin))) {
		perror("bind");
		return 1;
	}

	if (listen(s, BACKLOG)) {
		perror("listen");
		return 1;
	}

	for(;;) {

		ss = accept(s, NULL, NULL);

		if (ss < 0) {
			perror("accept");
			continue;
		}

		if (timeout.tv_sec) {
			if (setsockopt(ss, SOL_SOCKET, SO_RCVTIMEO, 
					&timeout, sizeof(timeout))
				|| setsockopt(ss, SOL_SOCKET, SO_RCVTIMEO, 
					&timeout, sizeof(timeout)))
			{
				perror("IO timeout");
				continue;
			}
		}

		if (pthread_create(&pt, NULL, http_handler, (void*)(long)ss)) {
			fprintf(stderr, "pthread_create failed\n");
			continue;
		}
	}

	close(s);

	return 0;
}

