// Endfield parent-process launch bridge for Windows / Wine / DW-Proton.
//
// OneChannelLC.dll requires the game's direct parent to be named Games.exe and
// carry a Hypergryph / Gryphline Authenticode signature. This helper:
//   1. copies a signed game-dir helper to %TEMP% as Games.exe
//   2. creates that copy CREATE_SUSPENDED (never resumed)
//   3. CreateProcessW Endfield.exe with PROC_THREAD_ATTRIBUTE_PARENT_PROCESS
//   4. waits for the game, then terminates and deletes the decoy
//
// See LAUNCHER_CHECK.md and https://github.com/misaka10843/Hi3Helper.Plugin.Hypergryph/issues/21

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const wchar_t* kParentSources[] = {
    L"Games.exe",
    L"PlatformProcess.exe",
    L"QtWebEngineProcess.exe",
    L"UnityCrashHandler64.exe",
    L"Endfield.exe",
};

std::wstring Quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        result.append(ch == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

std::wstring WinErr(const wchar_t* op, DWORD code = GetLastError()) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::wstring result = std::wstring(op) + L" (Win32 " + std::to_wstring(code) + L")";
    if (message) {
        result += L"\n";
        result += message;
        LocalFree(message);
    }
    return result;
}

void Fail(const std::wstring& message, bool popup) {
    std::wcerr << message << L'\n';
    if (popup) {
        MessageBoxW(nullptr, message.c_str(), L"Endfield Launch Bridge",
            MB_OK | MB_ICONERROR);
    }
}

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    HANDLE get() const { return value_; }
    void reset(HANDLE value = nullptr) {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_ = nullptr;
};

struct Options {
    fs::path game;
    fs::path parent_source;
    std::vector<std::wstring> arguments;
    bool hide_console = false;
    bool no_popup = false;
    bool no_default_args = false;
};

bool ParseArgs(int argc, wchar_t** argv, Options& options, std::wstring& error) {
    options = {};
    bool passthrough = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (passthrough) {
            options.arguments.push_back(arg);
            continue;
        }
        if (arg == L"--") {
            passthrough = true;
            continue;
        }
        if (arg == L"--game" && i + 1 < argc) {
            options.game = argv[++i];
            continue;
        }
        if (arg == L"--parent" && i + 1 < argc) {
            options.parent_source = argv[++i];
            continue;
        }
        if (arg == L"--hide-console") {
            options.hide_console = true;
            continue;
        }
        if (arg == L"--no-popup") {
            options.no_popup = true;
            continue;
        }
        if (arg == L"--no-default-args") {
            options.no_default_args = true;
            continue;
        }
        if (arg == L"--help" || arg == L"-h") {
            error = L"usage: endfield_bridge.exe [--game Endfield.exe] [--parent signed.exe] "
                    L"[--hide-console] [--no-popup] [--no-default-args] [--] [game args...]";
            return false;
        }
        // Ignore Steam %command% noise when this exe is placed first in launch options.
        if (arg.find(L".exe") != std::wstring::npos || arg.find(L".EXE") != std::wstring::npos) {
            fs::path candidate = arg;
            std::error_code ec;
            if (_wcsicmp(candidate.filename().c_str(), L"Endfield.exe") == 0 &&
                fs::is_regular_file(candidate, ec)) {
                options.game = candidate;
                continue;
            }
        }
        options.arguments.push_back(arg);
    }
    return true;
}

fs::path SelfDir() {
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return fs::path(path).parent_path();
}

fs::path ResolveGame(const Options& options, std::wstring& error) {
    std::error_code ec;
    if (!options.game.empty()) {
        auto game = fs::absolute(options.game, ec).lexically_normal();
        if (!fs::is_regular_file(game, ec)) {
            error = L"Game not found: " + game.wstring();
            return {};
        }
        return game;
    }
    const auto beside = SelfDir() / L"Endfield.exe";
    if (fs::is_regular_file(beside, ec)) return fs::absolute(beside).lexically_normal();
    error = L"Put endfield_bridge.exe next to Endfield.exe, or pass --game <path>.";
    return {};
}

fs::path PickParentSource(const fs::path& game, const fs::path& requested,
    std::wstring& error) {
    std::error_code ec;
    if (!requested.empty()) {
        auto path = fs::absolute(requested, ec).lexically_normal();
        if (!fs::is_regular_file(path, ec)) {
            error = L"Parent source not found: " + path.wstring();
            return {};
        }
        return path;
    }
    const auto dir = game.parent_path();
    for (const wchar_t* name : kParentSources) {
        const auto candidate = dir / name;
        if (fs::is_regular_file(candidate, ec)) return fs::absolute(candidate).lexically_normal();
    }

    // Official Hypergryph / GRYPHLINK launcher installs next to the games tree.
    for (const auto& relative : {
             fs::path(L"..") / L"Games.exe",
             fs::path(L"..") / L".." / L"Games.exe",
             fs::path(L"..") / L"Launcher.exe",
             fs::path(L"..") / L".." / L"Launcher.exe",
         }) {
        const auto candidate = fs::weakly_canonical(dir / relative, ec);
        if (!ec && fs::is_regular_file(candidate, ec)) return candidate;
    }

    error = L"No signed parent candidate found beside the game "
            L"(Games.exe / PlatformProcess.exe / UnityCrashHandler64.exe / ...).";
    return {};
}

fs::path StageParentCopy(const fs::path& source, std::wstring& error) {
    wchar_t temp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp)) {
        error = WinErr(L"GetTempPathW");
        return {};
    }
    const auto root = fs::path(temp) / L"EndfieldLaunchBridge" /
        std::to_wstring(GetCurrentProcessId());
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        error = L"Cannot create temp dir: " + root.wstring();
        return {};
    }
    const auto decoy = root / L"Games.exe";
    fs::copy_file(source, decoy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = L"Failed to copy parent image to " + decoy.wstring() + L": " +
            fs::path(ec.message()).wstring();
        return {};
    }
    return decoy;
}

bool CreateSuspended(const fs::path& exe, PROCESS_INFORMATION& info, std::wstring& error) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    auto command = Quote(exe.wstring());
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, exe.parent_path().c_str(),
            &startup, &info)) {
        error = WinErr(L"Create suspended Games.exe");
        return false;
    }
    return true;
}

bool CreateGameWithParent(HANDLE parent, const fs::path& game,
    const std::vector<std::wstring>& arguments, PROCESS_INFORMATION& info,
    std::wstring& error) {
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    if (!size) {
        error = WinErr(L"Prepare parent attributes");
        return false;
    }
    std::vector<unsigned char> storage(size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
        error = WinErr(L"InitializeProcThreadAttributeList");
        return false;
    }
    struct Cleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~Cleanup() { DeleteProcThreadAttributeList(list); }
    } cleanup{attributes};

    HANDLE parent_handle = parent;
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
            &parent_handle, sizeof(parent_handle), nullptr, nullptr)) {
        error = WinErr(L"UpdateProcThreadAttribute(PARENT_PROCESS)");
        return false;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    auto command = Quote(game.wstring());
    for (const auto& argument : arguments) command += L" " + Quote(argument);

    if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr,
            game.parent_path().c_str(), &startup.StartupInfo, &info)) {
        error = WinErr(L"Create Endfield.exe with official parent");
        return false;
    }
    return true;
}

void CleanupDecoy(HANDLE parent_process, const fs::path& decoy) {
    if (parent_process) {
        if (WaitForSingleObject(parent_process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(parent_process, 0);
        }
        WaitForSingleObject(parent_process, 5000);
    }
    std::error_code ec;
    if (!decoy.empty()) {
        fs::remove(decoy, ec);
        fs::remove(decoy.parent_path(), ec);
    }
}

int Run(Options options) {
    if (options.hide_console) FreeConsole();

    std::wstring error;
    const auto game = ResolveGame(options, error);
    if (game.empty()) {
        Fail(error, !options.no_popup);
        return 1;
    }

    const auto parent_source = PickParentSource(game, options.parent_source, error);
    if (parent_source.empty()) {
        Fail(error, !options.no_popup);
        return 1;
    }

    const auto decoy = StageParentCopy(parent_source, error);
    if (decoy.empty()) {
        Fail(error, !options.no_popup);
        return 1;
    }

    std::vector<std::wstring> arguments = options.arguments;
    if (!options.no_default_args && arguments.empty()) {
        // Match official launcher argv from games.log.
        arguments.push_back(L"-launcher_lang=zh-cn");
        arguments.push_back(L"-launcher_sub_channel=1");
    }

    PROCESS_INFORMATION parent_info{};
    if (!CreateSuspended(decoy, parent_info, error)) {
        CleanupDecoy(nullptr, decoy);
        Fail(error, !options.no_popup);
        return 1;
    }
    Handle parent_process(parent_info.hProcess);
    CloseHandle(parent_info.hThread);

    PROCESS_INFORMATION game_info{};
    if (!CreateGameWithParent(parent_process.get(), game, arguments, game_info, error)) {
        CleanupDecoy(parent_process.get(), decoy);
        Fail(error, !options.no_popup);
        return 1;
    }
    Handle game_process(game_info.hProcess);
    CloseHandle(game_info.hThread);

    std::wcerr << L"[endfield_bridge] parent=" << decoy.wstring()
               << L" (from " << parent_source.filename().wstring() << L")\n"
               << L"[endfield_bridge] game=" << game.wstring()
               << L" pid=" << game_info.dwProcessId << L'\n';

    const DWORD wait = WaitForSingleObject(game_process.get(), INFINITE);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(game_process.get(), &exit_code);

    CleanupDecoy(parent_process.get(), decoy);
    parent_process.reset();
    return static_cast<int>(exit_code);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    std::wstring error;
    if (!ParseArgs(argc, argv, options, error)) {
        Fail(error.empty() ? L"Invalid arguments." : error, true);
        return 2;
    }
    return Run(std::move(options));
}
