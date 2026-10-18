#pragma once
#include <string>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    using ProcessHandle = HANDLE;
#else
#include <sys/types.h>
using ProcessHandle = pid_t;
#endif

namespace civ {

    class ProcessPool {
    public:
        struct WorkerSpec {
            std::string executable;
            std::vector<std::string> args;
        };

        static ProcessHandle spawn(const WorkerSpec& spec);
        static void wait_all(const std::vector<ProcessHandle>& handles);
        static void kill_all(const std::vector<ProcessHandle>& handles);
    };

} // namespace civ