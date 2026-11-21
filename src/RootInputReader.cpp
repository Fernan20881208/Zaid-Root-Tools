#include "RootInputReader.hpp"
#include "RawEvdev.hpp"
#include <Geode/Geode.hpp>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace geode::prelude;

namespace zaid {
namespace {
// The only variable interpolated into this script is an integer from 0..63.
// No user string or arbitrary executable path ever goes to the root shell.
std::string rootCommand(int device) {
    const auto path = "/dev/input/event" + std::to_string(device);
    // No getevent executable is needed; our embedded C++ parser reads raw
    // struct input_event records emitted by the stock Android cat utility.
    // The privileged shell owns its cat child and terminates it on stdin EOF.
    return "exec /system/bin/sh -c '"
           "test -c " + path + " || exit 41; "
           "test -x /system/bin/cat || exit 42; "
           "( : < " + path + " ) 2>/dev/null || exit 44; "
           "printf \"ZAID_READY\\n\"; "
           "/system/bin/cat " + path + " & child=$!; "
           "trap \"kill $child 2>/dev/null\" EXIT HUP INT TERM; "
           "while IFS= read -r _; do :; done; "
           "kill $child 2>/dev/null; wait $child 2>/dev/null'";
}

void closeIfOpen(int& fd) {
    if (fd >= 0) { ::close(fd); fd = -1; }
}

bool spawnRoot(int device, pid_t& pid, int& inputFd, int& outputFd) {
    int inPipe[2] = {-1, -1};
    int outPipe[2] = {-1, -1};
    if (::pipe(inPipe) != 0) return false;
    if (::pipe(outPipe) != 0) {
        closeIfOpen(inPipe[0]); closeIfOpen(inPipe[1]);
        return false;
    }
    // Build the root command BEFORE fork: the child only performs async-safe
    // syscalls and exec. The game is a heavily multi-threaded process.
    auto command = rootCommand(device);
    pid = ::fork();
    if (pid == 0) {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        ::dup2(inPipe[0], STDIN_FILENO);
        ::dup2(outPipe[1], STDOUT_FILENO);
        int nullFd = ::open("/dev/null", O_WRONLY);
        if (nullFd >= 0) { ::dup2(nullFd, STDERR_FILENO); ::close(nullFd); }
        for (int i = 3; i < 1024; ++i) ::close(i);
        // KernelSU/Magisk/APatch provide su through the app's PATH.
        // No system-wide permissions, chmod, or policy changes are made.
        const char* argv[] = {"su", "-c", command.c_str(), nullptr};
        ::execvp("su", const_cast<char* const*>(argv));
        ::_exit(127);
    }
    closeIfOpen(inPipe[0]);
    closeIfOpen(outPipe[1]);
    if (pid < 0) {
        closeIfOpen(inPipe[1]); closeIfOpen(outPipe[0]);
        return false;
    }
    inputFd = inPipe[1];
    outputFd = outPipe[0];
    auto flags = ::fcntl(outputFd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(outputFd, F_SETFL, flags | O_NONBLOCK);
    return true;
}

void reapChild(pid_t pid) {
    if (pid <= 0) return;
    int code = 0;
    // Never freeze GD's main thread. This function runs on the worker.
    for (int n = 0; n < 60; ++n) {
        auto got = ::waitpid(pid, &code, WNOHANG);
        if (got == pid || (got < 0 && errno == ECHILD)) return;
        ::usleep(25000);
    }
    // Best-effort cleanup: this succeeds if su is still our UID, but an
    // approved su root process may refuse termination from the game UID.
    ::kill(pid, SIGTERM);
    ::waitpid(pid, &code, WNOHANG);
}

bool configHasValidBinding(InputConfig const& config) {
    for (auto const& binding : config.bindings) {
        if (binding.key >= 0 && binding.action != ActionType::None) return true;
    }
    return false;
}
}

RootInputReader& RootInputReader::instance() {
    // Geode mod modules live until process exit. Intentionally retain this
    // service: on Android process exit its pipe closes and the root shell
    // receives EOF, terminating its own cat child.
    static RootInputReader* reader = new RootInputReader();
    return *reader;
}

void RootInputReader::start() {
    std::lock_guard guard(m_mutex);
    if (m_started) return;
    m_started = true;
    m_worker = std::thread([this] { workerLoop(); });
    m_worker.detach();
}

bool RootInputReader::applyEffectiveConfigLocked() {
    auto effective = m_preference;
    // The Linux input stream must never run outside an actual game level.
    effective.enabled = effective.enabled && m_gameplayActive && configHasValidBinding(effective);
    if (effective == m_requested && m_requestedGeneration != 0) return false;
    m_requested = effective;
    ++m_requestedGeneration;
    m_generation.store(m_requestedGeneration);
    m_status.store(ReaderStatus::Off);
    return true;
}

void RootInputReader::configure(InputConfig const& config) {
    bool changed;
    {
        std::lock_guard guard(m_mutex);
        m_preference = config;
        changed = applyEffectiveConfigLocked();
    }
    if (!changed) return;
    releaseMappedInputs();
    m_condition.notify_one();
}

void RootInputReader::setGameplayActive(bool active) {
    bool changed;
    {
        std::lock_guard guard(m_mutex);
        m_gameplayActive = active;
        changed = applyEffectiveConfigLocked();
    }
    if (!changed) return;
    releaseMappedInputs();
    m_condition.notify_one();
}

bool RootInputReader::shouldEnd(uint64_t session) {
    return m_generation.load() != session;
}

void RootInputReader::postKey(uint64_t session, InputConfig const& config, int code, bool down) {
    bool mapped = false;
    for (auto const& binding : config.bindings) {
        if (binding.key == code && binding.action != ActionType::None) {
            mapped = true;
            break;
        }
    }
    if (!mapped) return;
    Loader::get()->queueInMainThread([session, config, code, down] {
        if (RootInputReader::instance().generation() != session) return;
        handleMappedInput(code, down, config);
    });
}

void RootInputReader::workerLoop() {
    uint64_t seen = 0;
    for (;;) {
        InputConfig config;
        uint64_t session;
        {
            std::unique_lock guard(m_mutex);
            m_condition.wait(guard, [&] { return m_requestedGeneration != seen; });
            session = m_requestedGeneration;
            seen = session;
            config = m_requested;
        }
        if (!config.enabled) { m_status.store(ReaderStatus::Off); continue; }
        if (config.device < 0 || config.device > 63) {
            m_status.store(ReaderStatus::Error);
            log::warn("Root Input Mapper: seleccion o configuracion invalida");
            continue;
        }
        m_status.store(ReaderStatus::Waiting);
        runSession(config, session);
        // A broken su/getevent will NOT retry in a tight loop. Toggle the
        // setting off and on to request a fresh authorization attempt.
    }
}

void RootInputReader::runSession(InputConfig const& config, uint64_t session) {
    int in = -1, out = -1;
    pid_t pid = -1;
    if (!spawnRoot(config.device, pid, in, out)) {
        m_status.store(ReaderStatus::Error);
        log::warn("Root Input Mapper: no se pudo crear la sesion su");
        return;
    }
    bool ready = false;
    bool streamEnded = false;
    std::string banner;
    banner.reserve(32);
    RawEvdev decoder;
    auto begun = std::chrono::steady_clock::now();
    while (!shouldEnd(session) && !streamEnded) {
        if (!ready && std::chrono::steady_clock::now() - begun > std::chrono::seconds(20)) break;
        struct pollfd pfd = {out, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
        auto result = ::poll(&pfd, 1, 75);
        if (result < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (result == 0) continue;
        uint8_t buffer[1024];
        auto n = ::read(out, buffer, sizeof(buffer));
        if (n == 0) { streamEnded = true; break; }
        if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; break; }
        for (ssize_t i = 0; i < n; ++i) {
            uint8_t byte = buffer[i];
            if (!ready) {
                if (byte != '\n') {
                    if (banner.size() < 96) banner.push_back(static_cast<char>(byte));
                    else streamEnded = true;
                    continue;
                }
                if (banner != "ZAID_READY") { streamEnded = true; break; }
                ready = true;
                if (!shouldEnd(session)) m_status.store(ReaderStatus::Active);
                continue;
            }
            if (auto key = decoder.push(byte))
                postKey(session, config, static_cast<int>(key->code), key->down);
        }
    }
    // EOF on stdin signals root shell to terminate the cat child.
    closeIfOpen(in);
    closeIfOpen(out);
    reapChild(pid);
    if (shouldEnd(session)) return;
    m_status.store(ReaderStatus::Error);
    log::warn("Root Input Mapper: su/evdev termino; comprueba root y dispositivo");
    Loader::get()->queueInMainThread([session] {
        if (RootInputReader::instance().generation() == session)
            releaseMappedInputs();
    });
}
}
