#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define BACKLOG 10

volatile sig_atomic_t sighup_received = 0;

static int client_fd = -1;

static void handle_sighup(int signo) {
    (void)signo;
    sighup_received = 1;
}

int create_listener(void) {
    int listen_fd = -1;
    int opt = 1;
    struct sockaddr_in addr;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return -1;
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        close(listen_fd);
        return -1;
    }
#ifdef SO_REUSEPORT
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEPORT (warning)");
    }
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, BACKLOG) == -1) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    printf("LISTENER: bound to port %d (fd %d)\n", PORT, listen_fd);
    return listen_fd;
}

int install_sighup_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sighup;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    printf("SIGNAL: SIGHUP handler installed.\n");
    return 0;
}

void event_loop(int listen_fd, const sigset_t *pselect_unmask) {
    int maxfd;
    for (;;) {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(listen_fd, &readset);
        maxfd = listen_fd;

        if (client_fd != -1) {
            FD_SET(client_fd, &readset);
            if (client_fd > maxfd) maxfd = client_fd;
        }

        int ready = pselect(maxfd + 1, &readset, NULL, NULL, NULL, pselect_unmask);

        if (sighup_received) {
            sighup_received = 0;
            printf("EVENT: caught SIGHUP (handled in main loop).\n");
        }

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("pselect");
                break;
            }
        }

        if (ready == 0) {
            continue;
        }

        if (FD_ISSET(listen_fd, &readset)) {
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            int newfd = accept(listen_fd, (struct sockaddr *)&peer, &plen);
            if (newfd == -1) {
                perror("accept");
            } else {
                char addrbuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &peer.sin_addr, addrbuf, sizeof(addrbuf));
                printf("EVENT: incoming connection from %s:%d, fd %d\n",
                       addrbuf, ntohs(peer.sin_port), newfd);

                if (client_fd == -1) {
                    client_fd = newfd;
                    printf("SERVER: accepted client on fd %d\n", client_fd);
                } else {
                    printf("SERVER: already have client (fd %d), closing extra fd %d\n",
                           client_fd, newfd);
                    close(newfd);
                }
            }
        }

        if (client_fd != -1 && FD_ISSET(client_fd, &readset)) {
            char buf[BUFFER_SIZE];
            ssize_t n = read(client_fd, buf, sizeof(buf));
            if (n > 0) {
                printf("EVENT: read %zd bytes from client fd %d\n", n, client_fd);
            } else if (n == 0) {
                printf("SERVER: client disconnected, closing fd %d\n", client_fd);
                close(client_fd);
                client_fd = -1;
            } else {
                if (errno == EINTR) {
                    continue;
                } else {
                    perror("read");
                    close(client_fd);
                    client_fd = -1;
                }
            }
        }
    } /* for */
}

int main(void) {
    sigset_t block_mask, orig_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &block_mask, &orig_mask) == -1) {
        perror("sigprocmask block");
        return EXIT_FAILURE;
    }

    if (install_sighup_handler() == -1) {
        sigprocmask(SIG_SETMASK, &orig_mask, NULL);
        return EXIT_FAILURE;
    }

    int listen_fd = create_listener();
    if (listen_fd == -1) {
        sigprocmask(SIG_SETMASK, &orig_mask, NULL);
        return EXIT_FAILURE;
    }

    printf("SIGNAL: SIGHUP blocked before entering pselect loop.\n");

    event_loop(listen_fd, &orig_mask);

    if (client_fd != -1) close(client_fd);
    close(listen_fd);

    sigprocmask(SIG_SETMASK, &orig_mask, NULL);

    return EXIT_SUCCESS;
}
