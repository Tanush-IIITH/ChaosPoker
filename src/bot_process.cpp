#include "bot_process.h"

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include <cstring>
#include <stdexcept>

BotProcess::BotProcess(const std::string& command) {
    int pipe_in[2];   // parent writes to pipe_in[1], child reads from pipe_in[0]
    int pipe_out[2];  // child writes to pipe_out[1], parent reads from pipe_out[0]

    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
        throw std::runtime_error("pipe() failed: " + std::string(strerror(errno)));
    }

    pid_ = fork();
    if (pid_ < 0) {
        throw std::runtime_error("fork() failed: " + std::string(strerror(errno)));
    }

    if (pid_ == 0) {
        // child process
        close(pipe_in[1]);   // close write end of input pipe
        close(pipe_out[0]);  // close read end of output pipe

        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);

        close(pipe_in[0]);
        close(pipe_out[1]);

        // exec the bot
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127); // exec failed
    }

    // parent process
    close(pipe_in[0]);   // close read end of input pipe
    close(pipe_out[1]);  // close write end of output pipe

    stdin_fd_ = pipe_in[1];
    stdout_fd_ = pipe_out[0];
}

BotProcess::~BotProcess() {
    kill();
    close_pipes();
}

BotProcess::BotProcess(BotProcess&& other) noexcept
    : pid_(other.pid_), stdin_fd_(other.stdin_fd_),
      stdout_fd_(other.stdout_fd_), read_buf_(std::move(other.read_buf_)) {
    other.pid_ = -1;
    other.stdin_fd_ = -1;
    other.stdout_fd_ = -1;
}

BotProcess& BotProcess::operator=(BotProcess&& other) noexcept {
    if (this != &other) {
        kill();
        close_pipes();
        pid_ = other.pid_;
        stdin_fd_ = other.stdin_fd_;
        stdout_fd_ = other.stdout_fd_;
        read_buf_ = std::move(other.read_buf_);
        other.pid_ = -1;
        other.stdin_fd_ = -1;
        other.stdout_fd_ = -1;
    }
    return *this;
}

bool BotProcess::write_line(const std::string& msg) {
    if (stdin_fd_ < 0) return false;

    std::string line = msg + "\n";
    const char* data = line.c_str();
    size_t remaining = line.size();

    while (remaining > 0) {
        ssize_t written = write(stdin_fd_, data, remaining);
        if (written <= 0) return false;
        data += written;
        remaining -= written;
    }
    return true;
}

std::string BotProcess::read_line(int timeout_ms) {
    if (stdout_fd_ < 0) return "";

    // check if we already have a complete line in the buffer
    auto nl = read_buf_.find('\n');
    if (nl != std::string::npos) {
        std::string line = read_buf_.substr(0, nl);
        read_buf_.erase(0, nl + 1);
        // strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    }

    // poll for data with timeout
    struct pollfd pfd;
    pfd.fd = stdout_fd_;
    pfd.events = POLLIN;

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        int remaining = timeout_ms - elapsed;
        int ret = poll(&pfd, 1, remaining);

        if (ret < 0) {
            if (errno == EINTR) continue;
            return "";
        }
        if (ret == 0) return ""; // timeout

        if (pfd.revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t n = read(stdout_fd_, buf, sizeof(buf));
            if (n <= 0) return ""; // EOF or error

            read_buf_.append(buf, n);

            nl = read_buf_.find('\n');
            if (nl != std::string::npos) {
                std::string line = read_buf_.substr(0, nl);
                read_buf_.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return line;
            }
        }

        if (pfd.revents & (POLLERR | POLLNVAL)) return "";
    }

    return ""; // timeout
}

bool BotProcess::is_alive() const {
    if (pid_ <= 0) return false;
    int status;
    pid_t result = waitpid(pid_, &status, WNOHANG);
    return result == 0; // 0 means child still running
}

void BotProcess::kill() {
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        int status;
        waitpid(pid_, &status, 0);
        pid_ = -1;
    }
}

void BotProcess::close_pipes() {
    if (stdin_fd_ >= 0) { close(stdin_fd_); stdin_fd_ = -1; }
    if (stdout_fd_ >= 0) { close(stdout_fd_); stdout_fd_ = -1; }
}
