#ifndef NETWORKS_COURSE_HTTP_H
#define NETWORKS_COURSE_HTTP_H

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>


typedef enum {
    METHOD_GET = 0,
    METHOD_HEAD = 1,
} http_method_t;

typedef enum {
    RESPONSE_CODE_OK = 0, // 200
    RESPONSE_CODE_FORBIDDEN = 1, // 403
    RESPONSE_CODE_NOT_FOUND = 2, // 404
    RESPONSE_CODE_METHOD_NOT_ALLOWED = 3, // 405
    RESPONSE_CODE_BAD_REQUEST = 4, // 400
    RESPONSE_CODE_INTERNAL_SERVER_ERROR = 5, // 500
} http_response_code_t;

typedef struct {
    http_method_t method;
    char uri[10000];
    char version[10];
    int fd;
} http_request_t;

static char *http_methods_map[] = {
    [METHOD_GET] = "GET",
    [METHOD_HEAD] = "HEAD",
};

typedef enum {
    STATE_CONNECT,
    STATE_READ,
    STATE_SEND,
    STATE_COMPLETE,
    STATE_ERROR
} client_state_t;
#define BUF_SIZE 10000
#define CLIENT_NUM 65536
#define POOL_SIZE 16
#define MAX_EVENTS 8
#define MAX_PAYLOAD 8192

typedef struct {
    int socket;
    client_state_t state;
    // char read_buffer[BUF_SIZE];
    ssize_t bytes_read;
    ssize_t bytes_to_send;
    char *send_buffer;
    http_request_t *req;
} client_request_t;

int http_parse_request(char *raw, http_request_t *req);
int http_write_error(http_request_t *req, http_response_code_t code);
int http_handle( client_request_t *request);

#endif //NETWORKS_COURSE_HTTP_H
