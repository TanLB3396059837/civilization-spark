#include "civ/process/process_pool.hpp"
#include <stdexcept>

#ifdef _WIN32

namespace civ {

static std::string build_cmd(const ProcessPool::WorkerSpec& spec) {
    std::string cmd = "\"" + spec.executable + "\"";
    for (const auto& a : spec.args) cmd += " \"" + a + "\"";
    return cmd;
}

ProcessHandle ProcessPool::spawn(const WorkerSpec& spec) {
    std::string cmd = build_cmd(spec);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr,
                             FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok) throw std::runtime_error("CreateProcess failed");
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

void ProcessPool::wait_all(const std::vector<ProcessHandle>& hs) {
    for (auto h : hs) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
}

void ProcessPool::kill_all(const std::vector<ProcessHandle>& hs) {
    for (auto h : hs) {
        TerminateProcess(h, 1);
        WaitForSingleObject(h, 1000);
        CloseHandle(h);
    }
}

} // namespace civ

#else

#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

namespace civ {

ProcessHandle ProcessPool::spawn(const WorkerSpec& spec) {
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork failed");
    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(spec.executable.c_str()));
        for (const auto& a : spec.args)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(spec.executable.c_str(), argv.data());
        _exit(127);
    }
    return pid;
}

void ProcessPool::wait_all(const std::vector<ProcessHandle>& hs) {
    for (auto h : hs) { int s; waitpid(h, &s, 0); }
}

void ProcessPool::kill_all(const std::vector<ProcessHandle>& hs) {
    for (auto h : hs) ::kill(h, SIGTERM);
    for (auto h : hs) ::waitpid(h, nullptr, 0);
}

} // namespace civ

#endif