#include <iostream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>

class RedisServer {
private:
    std::unordered_map<std::string, std::string> database;

    std::vector<std::string> parseCommand(const std::string& command) {
        std::istringstream iss(command);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    std::string executeCommand(const std::vector<std::string>& tokens) {
        if (tokens.empty()) return "ERROR: Empty command";

        const std::string& cmd = tokens[0];
        if (cmd == "SET" && tokens.size() == 3) {
            database[tokens[1]] = tokens[2];
            return "OK";
        } else if (cmd == "GET" && tokens.size() == 2) {
            auto it = database.find(tokens[1]);
            if (it != database.end()) {
                return it->second;
            } else {
                return "nil";
            }
        } else if (cmd == "DEL" && tokens.size() == 2) {
            auto it = database.find(tokens[1]);
            if (it != database.end()) {
                database.erase(it);
                return "1";
            } else {
                return "0";
            }
        } else {
            return "ERROR: Unknown or invalid command";
        }
    }

public:
    void start(int port) {
        int server_fd, new_socket;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("Socket failed");
            exit(EXIT_FAILURE);
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            perror("Setsockopt failed");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }

        if (listen(server_fd, 3) < 0) {
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }

        std::cout << "Redis-like server is running on port " << port << std::endl;

        while (true) {
            if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
                perror("Accept failed");
                exit(EXIT_FAILURE);
            }

            char buffer[1024] = {0};
            read(new_socket, buffer, 1024);
            std::string command(buffer);
            std::vector<std::string> tokens = parseCommand(command);
            std::string response = executeCommand(tokens);
            send(new_socket, response.c_str(), response.size(), 0);
            close(new_socket);
        }
    }
};

int main() {
    RedisServer server;
    server.start(6379);
    return 0;
}