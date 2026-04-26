#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "z.hpp"
#include "z_ev.hpp"

struct tcp_echo final : z_Task {
    z_fields(z_ev_read read; z_ev_write write);
    ev_io io;
    char buf[128];
    ssize_t len;

    z_impl_resume();

    z_define_deinit(tcp_echo) {
        z_ev::io_stop(&io);
        close(io.fd);
    }

    tcp_echo(int fd) {
        ev_io_init(&io, nullptr, fd, 0);
    }

    z_function(void) {
        z_begin();

        for (;;) {
            z_call(read, &len, &io, &buf, sizeof(buf));
            if (len < 0) {
                perror("read err");
                break;
            }
            if (len == 0) {
                printf("fd:%d (close)\n", io.fd);
                break;
            }
            if (buf[len-1] == '\n') --len;
            if (len == 0) continue;
            printf("fd:%d msg:'%.*s'\n", io.fd, (int)len, buf);
            z_call(write, nullptr, &io, &buf, len);
        }

        z_ret();
    }
};

struct tcp_server final : z_Task {
    z_fields(z_ev_accept accept);
    ev_io io;
    int port;

    z_impl_resume();

    z_define_deinit(tcp_server) {
        z_ev::io_stop(&io);
        close(io.fd);
    }

    tcp_server(int port) : port(port) {
        ev_io_init(&io, nullptr, -1, 0);
    }

    bool init() {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_port = htons(port);

        if (bind(fd, (sockaddr *)&sin, sizeof(sin)) < 0 || listen(fd, 128) < 0) {
            perror("bind or listen err");
            close(fd);
            return false;
        }

        ev_io_init(&io, nullptr, fd, 0);
        return true;
    };

    z_function(void) {
        z_begin();

        if (!init()) z_ret();

        for (;;) {
            int cfd;
            z_call(accept, &cfd, &io);
            if (cfd < 0) {
                perror("accept err");
                break;
            }
            z_launch(tcp_echo, cfd);
        }

        z_ret();
    }
};

int main() {
    z_ev::init();

    z_launch(tcp_server, 8888);
    z_launch(tcp_server, 8888); // fail-fast
    z_launch(tcp_server, 8889);

    z_ev::run();

    return 0;
}
