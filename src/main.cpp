#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>
#include <poll.h>
#include <mutex>

#include <vector>

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

const int32_t max_buff_size = 4096;

struct Connection {
    int fd = -1;

    bool want_read = false;
    bool want_write = false;
    bool want_close = false;

    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
};

class ConnectionPool {
private:
    std::vector<Connection *> pool;
    std::mutex _mutex;
    int max_pool_size = 100;
public:

    ConnectionPool(int size): max_pool_size(size) {
        for (int i = 0; i < size; ++i) {
            pool.push_back(new Connection());
        }
    }

    ~ConnectionPool() {
        for (auto* conn : pool) {
            delete conn;
        }
    }

    Connection* acquire() {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!pool.empty()) {
            Connection* conn = pool.back();
            pool.pop_back();
            return conn;
        }
        return new Connection();
    }

    void release(Connection* conn) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (pool.size() < max_pool_size) {
            conn->fd = -1;
            conn->incoming.clear();
            conn->outgoing.clear();
            conn->want_read = false;
            conn->want_write = false;
            conn->want_close = false;
            pool.push_back(conn);
        } else {
            delete conn;
        }
    }
};

static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl() error");
        return;
    }

    flags |= O_NONBLOCK;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl() error");
    }
}

static Connection *handle_accept(int fd, ConnectionPool& pool) {

    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr* )&client_addr, &addrlen);
    if (connfd < 0) {
        return NULL;
    }

    fd_set_nb(fd);

    Connection *conn = pool.acquire();
    conn->want_read = true;
    conn->fd = fd;
    return conn;
}

static void buff_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void buff_consume(std::vector<uint8_t> &buff, size_t n) {
    buff.erase(buff.begin(), buff.end() + n);
}

static bool one_request(Connection *conn) {
    if (conn->incoming.size() < 4) {
        return false;
    }

    uint8_t len = 0;
    memcpy(&len, &conn->incoming[0], 4);
    if (len > max_buff_size) {
        msg("too long");
        conn->want_close = true;
        return false;
    }
    if (4 + len > conn->incoming.size()) {
        return false;
    }
    const uint8_t *request = &conn->incoming[4];

    printf("client says: len:%d data:%.*s\n",
        len, len < 100 ? len : 100, request);

    buff_append(conn->outgoing, (const uint8_t *)&len, 4);
    buff_append(conn->outgoing, request, len);

    buff_consume(conn->incoming, 4 + len);

    return true;
}

static void handle_write(Connection*conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;
        return;
    }

    buff_consume(conn->outgoing, (size_t)rv);

    if (conn->outgoing.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

static void handle_read(Connection *conn) {
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv < 0 && errno == EAGAIN) {
        return; 
    }
    
    if (rv < 0) {
        msg_errno("read() error");
        conn->want_close = true;
        return;
    }

    if (rv == 0) {
        if (conn->incoming.size() == 0) {
            msg("client closed");
        } else {
            msg("unexpected EOF");
        }
        conn->want_close = true;
        return;
    }
  
    buff_append(conn->incoming, buf, (size_t)rv);

    while (one_request(conn)) {}

    if (conn->outgoing.size() > 0) { 
        conn->want_read = false;
        conn->want_write = true;
        return handle_write(conn);
    }
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

    ConnectionPool conn_pool(100);

    std::vector<Connection *> fd2conn;
    std::vector<struct pollfd> poll_args;
    while (true) {
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        for (Connection *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            if (conn->want_read) {
                pfd.events |= POLLIN;
            }
            if (conn->want_write) {
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if (rv < 0 && errno == EINTR) {
            continue; 
        }
        if (rv < 0) {
            die("poll");
        }

        if (poll_args[0].revents) {
            if (Connection* conn = handle_accept(fd, conn_pool)) {
                // Pool'dan yangi connection o'rniga
                Connection* pooled_conn = conn_pool.acquire();
                pooled_conn->fd = conn->fd;
                pooled_conn->want_read = true;
                
                delete conn;
                
                if (fd2conn.size() <= (size_t)pooled_conn->fd) {
                    fd2conn.resize(pooled_conn->fd + 1);
                }
                fd2conn[pooled_conn->fd] = pooled_conn;
            }
        }

        for (size_t i = 1; i < poll_args.size(); ++i) {
            uint32_t ready = poll_args[i].revents;
            if (ready == 0) {
                continue;
            }

            Connection *conn = fd2conn[poll_args[i].fd];
            if (ready & POLLIN) {
                assert(conn->want_read);
                handle_read(conn); 
            }
            if (ready & POLLOUT) {
                assert(conn->want_write);
                handle_write(conn);
            }

            if ((ready & POLLERR) || conn->want_close) {
                (void)close(conn->fd);
                fd2conn[conn->fd] = NULL;
                delete conn;
            }
        } 
    }

    return 0;
}