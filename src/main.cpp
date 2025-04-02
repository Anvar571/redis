#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static int32_t read_full(int sockfd, char *buff, size_t buff_size) {
    while (buff_size > 0) {
        ssize_t bytes_read = read(sockfd, buff, buff_size);
        if (bytes_read <= 0) {
            return -1;
        }
        buff_size -= (size_t)bytes_read;
        buff += bytes_read;
    }
    return 0;
}

static int32_t write_full(int sockfd, const char *buff, size_t buff_size) {
    while (buff_size > 0) {
        ssize_t rv = write(sockfd, buff, buff_size);
        if (rv <= 0) {
            return -1;
        }
        buff_size -= (size_t)rv;
        buff += rv;
    }
    return 0;
}

const int32_t max_buff_size = 4096;

static int32_t one_request(int sockfd) {
    // 4 bytes header
    char rbuf[4 + max_buff_size];
    errno = 0;
    int32_t err = read_full(sockfd, rbuf, 4);

    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    // read the header message
    uint32_t header_len = 0;
    memcpy(&header_len, rbuf, sizeof(header_len));
    if (header_len > max_buff_size) {
        msg("message too long");
        return -1;
    }

    // read the body message
    err = read_full(sockfd, &rbuf[4], header_len);
    if (err) {
        msg("read() error");
        return err;
    }

    // do something with the message
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    header_len = (uint32_t)strlen(reply);
    memcpy(wbuf, &header_len, 4);
    memcpy(&wbuf[4], reply, header_len);
    return write_full(sockfd, wbuf, 4 + header_len);
}

int main () {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        die("socket");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = ntohs(INADDR_ANY);
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv == -1) {
        die("bind");
    }

    rv = listen(fd, SOMAXCONN);
    if (rv == -1) {
        die("listen");
    }

    printf("Listening on port 1234...\n");
    fflush(stdout);

    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addr_len = sizeof(client_addr);

        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addr_len);
        if (connfd < 0) {
            continue;
        }

        printf("Accepted connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        while (true) {
            int32_t err = one_request(connfd);
            if (err) {
                break;
            }
        }

        close(connfd);
    }

    return 0;
}