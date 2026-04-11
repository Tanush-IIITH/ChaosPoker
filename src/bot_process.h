#pragma once

#include <string>
#include <sys/types.h>

class BotProcess {
public:
    // Launch a bot process. command is the executable path.
    explicit BotProcess(const std::string& command);
    ~BotProcess();

    // no copy
    BotProcess(const BotProcess&) = delete;
    BotProcess& operator=(const BotProcess&) = delete;

    // move
    BotProcess(BotProcess&& other) noexcept;
    BotProcess& operator=(BotProcess&& other) noexcept;

    // Write a line to the bot's stdin (appends newline).
    bool write_line(const std::string& msg);

    // Read a line from the bot's stdout with timeout in milliseconds.
    // Returns empty string on timeout or error.
    std::string read_line(int timeout_ms);

    bool is_alive() const;
    void kill();

    pid_t pid() const { return pid_; }

private:
    void close_pipes();

    pid_t pid_ = -1;
    int stdin_fd_ = -1;   // we write to this (bot's stdin)
    int stdout_fd_ = -1;  // we read from this (bot's stdout)
    std::string read_buf_; // leftover data from previous reads
};
