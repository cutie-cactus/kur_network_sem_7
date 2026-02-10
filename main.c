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

#include "http.h"
#include "errors.h"
#include "log.h"

int sockfd;
int epollfd;
client_request_t clients[CLIENT_NUM];
int running = 1;

void signal_handler(int signal) {
    log_info("graceful shutting down");
    running = 0;
    exit(0);
}

void accept_connection() {
    int fd = accept(sockfd, NULL, NULL);
    if (fd < 0) {
        log_error("accept failed");
        return;
    }
    // fcntl(fd, F_SETFL, O_NONBLOCK);
    
    struct epoll_event e;
    e.events = EPOLLIN | EPOLLET | EPOLLOUT;
    e.data.fd = fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &e) < 0) {
        log_fatal("epoll_ctl: failed to add socket");
    }

    clients[fd].socket = fd;
    clients[fd].state = STATE_CONNECT;
    clients[fd].bytes_read = 0;
    clients[fd].send_buffer = NULL;

    log_info("got new connection");
}

void read_request(client_request_t *request) {
    int fd = request->socket;
    char buf[BUF_SIZE] = {0};

    ssize_t n = read(fd, buf, sizeof(buf));
    if (n == 0) {
        log_info("connection is closed");
        return;
    }

    http_request_t req;
    req.fd = fd;
    int rc = http_parse_request(buf, &req);
    if (rc != OK) {
        log_error("cannot parse request: %s", error_messages[rc]);
        if (rc == ERR_UNSUPPORTED_METHOD) {
            http_write_error(&req, RESPONSE_CODE_METHOD_NOT_ALLOWED);
        }
        return;
    }
    request->req = &req;
    // request->state = STATE_SEND;
}

void use_fd(client_request_t *request) {
    int fd = request->socket;
    char buf[BUF_SIZE] = {0};

    ssize_t n = read(fd, buf, sizeof(buf));
    if (n == 0) {
        log_info("connection is closed");
        return;
    }

    http_request_t req;
    req.fd = fd;
    int rc = http_parse_request(buf, &req);
    if (rc != OK) {
        log_error("cannot parse request: %s", error_messages[rc]);
        if (rc == ERR_UNSUPPORTED_METHOD) {
            http_write_error(&req, RESPONSE_CODE_METHOD_NOT_ALLOWED);
        }
        return;
    }
    request->req = &req;
    rc = http_handle(request);
    if (rc != OK) {
        log_error("cannot handle request: %s", error_messages[rc]);
        return;
    }

    log_debug("%s \"%s\"", http_methods_map[req.method], req.uri);
}

void *handle_connections(void *data) {
    int rc;

    log_debug("starting processing");

    struct epoll_event events[MAX_EVENTS];

    while (running) {
        int n = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (!running) {
                break;
            }
            log_fatal("epoll failed");
        }
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == sockfd) {
                accept_connection();
                continue;
            }

            int fd = events[i].data.fd;
            client_request_t *request = &clients[fd];

            switch (request->state) {
                case STATE_CONNECT:
                    read_request(request);
                case STATE_SEND:
                    http_handle(request);
                    break;
                case STATE_COMPLETE:
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    log_info("connection closed");
                    request->socket = -1;
                    break;
                case STATE_ERROR:
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    log_info("connection closed");
                    request->socket = -1;
                    break;
            }
            if (request->state < STATE_COMPLETE) {
                struct epoll_event e;
                e.events = EPOLLIN | EPOLLET | EPOLLOUT;
                e.data.fd = fd;
                if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &e) < 0) {
                    log_fatal("epoll_ctl: failed to modify socket");
                }
            }
        }
    }

    pthread_exit((void *) 0);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 1024);

    if (argc != 2) {
        log_fatal("num of arguments must be 2, have only %d", argc);
    }

    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        log_fatal("cannot create signal handler for SIGINT");
    }
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        log_fatal("cannot create signal handler for SIGTERM");
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_fatal("cannot create socket");
    }

    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    short port = (short) atoi(argv[1]);
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
        log_fatal("cannot bind socket");
    }

    listen(sockfd, CLIENT_NUM);

    epollfd = epoll_create1(0);
    if (epollfd < 0) {
        log_fatal("cannot create epoll");
    }

    struct epoll_event e;
    e.events = EPOLLIN;
    e.data.fd = sockfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &e) < 0) {
        log_fatal("epoll_ctl failed on main socket");
    }

    log_info("starting server");

    int rc;
    pthread_t thread_pool[POOL_SIZE];

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pthread_create(&thread_pool[i], NULL, handle_connections, NULL) != 0) {
            log_fatal("cannot create thread №%d", i);
        }
    }
    handle_connections(NULL);
    for (int i = 0; i < POOL_SIZE; i++) {
        pthread_join(thread_pool[i], NULL);
    }

    close(sockfd);
    return 0;
}
