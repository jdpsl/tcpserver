#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <algorithm>

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void handle_client(int client_sock, const char *path) {
    int pipe_stdin[2], pipe_stdout[2], pipe_stderr[2];

    if (pipe(pipe_stdin) == -1 || pipe(pipe_stdout) == -1 || pipe(pipe_stderr) == -1) {
        error("pipe");
    }

    pid_t pid = fork();
    if (pid < 0) {
        error("fork");
    }

    if (pid == 0) {  // Child process
        close(pipe_stdin[1]);
        close(pipe_stdout[0]);
        close(pipe_stderr[0]);

        dup2(pipe_stdin[0], STDIN_FILENO);
        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stderr[1], STDERR_FILENO);

        execl(path, path, nullptr);
        error("execl");
    } else {  // Parent process
        close(pipe_stdin[0]);
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);

        fd_set read_fds;
        char buffer[256];
        int n;

        while (true) {
            FD_ZERO(&read_fds);
            FD_SET(client_sock, &read_fds);
            FD_SET(pipe_stdout[0], &read_fds);
            FD_SET(pipe_stderr[0], &read_fds);

            int max_fd = std::max(client_sock, std::max(pipe_stdout[0], pipe_stderr[0])) + 1;
            select(max_fd, &read_fds, nullptr, nullptr, nullptr);

            if (FD_ISSET(client_sock, &read_fds)) {
                n = read(client_sock, buffer, 255);
                if (n <= 0) break;
                write(pipe_stdin[1], buffer, n);
            }

            if (FD_ISSET(pipe_stdout[0], &read_fds)) {
                n = read(pipe_stdout[0], buffer, 255);
                if (n <= 0) break;
                write(client_sock, buffer, n);
            }

            if (FD_ISSET(pipe_stderr[0], &read_fds)) {
                n = read(pipe_stderr[0], buffer, 255);
                if (n <= 0) break;
                write(client_sock, buffer, n);
            }
        }

        close(client_sock);
        close(pipe_stdin[1]);
        close(pipe_stdout[0]);
        close(pipe_stderr[0]);
        waitpid(pid, nullptr, 0);
    }
}

int main(int argc, char *argv[]) {
    int port = 0;
    const char *path = nullptr;

    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = std::stoi(optarg);
                break;
            default:
                std::cerr << "Usage: " << argv[0] << " -p port path" << std::endl;
                return 1;
        }
    }

    if (optind < argc) {
        path = argv[optind];
    } else {
        std::cerr << "Usage: " << argv[0] << " -p port path" << std::endl;
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("socket");
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        error("bind");
    }

    listen(sockfd, 5);

    while (true) {
        struct sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);
        int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            error("accept");
        }

        if (fork() == 0) {
            close(sockfd);
            handle_client(newsockfd, path);
            exit(0);
        } else {
            close(newsockfd);
        }
    }

    close(sockfd);
    return 0;
}
