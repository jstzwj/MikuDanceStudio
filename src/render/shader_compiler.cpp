#include "mikudancestudio/shader_compiler.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <regex>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace mikudancestudio::rhi {
namespace {
namespace fs = std::filesystem;
struct Workspace {
    fs::path path;
    Workspace() {
        static std::atomic<unsigned> serial{0};
        std::random_device random;
        for (unsigned n = 0; n < 32; ++n) {
            auto candidate = fs::temp_directory_path() / ("mds-shader-" + std::to_string(random()) +
                                                          "-" + std::to_string(++serial));
            if (fs::create_directory(candidate)) {
                path = std::move(candidate);
                fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
                return;
            }
        }
        throw std::runtime_error("shader_workspace_unavailable");
    }
    ~Workspace() {
        std::error_code ec;
        if (!path.empty())
            fs::remove_all(path, ec);
    }
};
std::string Read(const fs::path& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    std::string value(limit, '\0');
    in.read(value.data(), value.size());
    value.resize(std::size_t(in.gcount()));
    return value;
}
#ifdef _WIN32
std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"')
            result.push_back(L'\\');
        result.push_back(c);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif
bool Run(const fs::path& executable, const std::vector<std::string>& arguments,
         const Workspace& workspace, unsigned timeout, std::string& diagnostic) {
    const auto log = workspace.path / "compiler.log";
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (output != INVALID_HANDLE_VALUE)
            CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        diagnostic = "shader_process_io_failed";
        return false;
    }
    std::wstring command = Quote(executable.wstring());
    for (const auto& arg : arguments)
        command += L" " + Quote(fs::u8path(arg).wstring());
    STARTUPINFOW start{sizeof(STARTUPINFOW)};
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = input;
    start.hStdOutput = start.hStdError = output;
    PROCESS_INFORMATION process{};
    const bool created =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, workspace.path.c_str(), &start, &process) != FALSE;
    CloseHandle(input);
    CloseHandle(output);
    if (!created) {
        diagnostic = "shader_process_creation_failed";
        return false;
    }
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, timeout);
    DWORD status = 1;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &status);
    else {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0) {
        diagnostic = "shader_compile_timeout";
        return false;
    }
    const bool success = status == 0;
#else
    std::vector<std::string> storage{executable.string()};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& item : storage)
        argv.push_back(item.data());
    argv.push_back(nullptr);
    const int output = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const int input = open("/dev/null", O_RDONLY);
    if (output < 0 || input < 0) {
        if (output >= 0)
            close(output);
        if (input >= 0)
            close(input);
        diagnostic = "shader_process_io_failed";
        return false;
    }
    // Only async-signal-safe calls in the child; argument storage is prepared
    // before fork. Never use a shell or interpret shader text as a command.
    const auto pid = fork();
    if (pid == 0) {
        if (chdir(workspace.path.c_str()) || dup2(input, 0) < 0 || dup2(output, 1) < 0 ||
            dup2(output, 2) < 0)
            _exit(126);
        close(input);
        close(output);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(input);
    close(output);
    if (pid < 0) {
        diagnostic = "shader_process_creation_failed";
        return false;
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    for (;;) {
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            break;
        if (result < 0 || std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            diagnostic = "shader_compile_timeout";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    diagnostic = Read(log, 1024 * 1024);
    if (!success && diagnostic.empty())
        diagnostic = "spirv_compile_failed";
    return success;
}
class DxcCompiler final : public SpirvCompiler {
    ShaderCompilerOptions options;
    bool available = false;
    std::string identity;
    mutable std::mutex mutex;
    std::map<std::string, SnapshotBytes> cache;
    std::size_t cachedBytes = 0;
    ShaderCompilerStats stats;

public:
    explicit DxcCompiler(ShaderCompilerOptions selected) : options(std::move(selected)) {
        if (options.executable.empty() || !options.timeoutMilliseconds ||
            options.timeoutMilliseconds > 60000)
            return;
        std::error_code ec;
        options.executable = fs::absolute(options.executable, ec);
        if (ec || !fs::is_regular_file(options.executable, ec))
            return;
        try {
            Workspace workspace;
            available = Run(options.executable, {"--version"}, workspace,
                            options.timeoutMilliseconds, identity);
            if (available)
                identity = options.executable.u8string() + "\n" + identity;
        } catch (const std::exception& e) {
            identity = e.what();
        }
    }
    bool Available() const override { return available; }
    const std::string& Identity() const override { return identity; }
    ShaderCompilerStats Stats() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return stats;
    }
    bool Compile(const ShaderModule& module, SnapshotBytes& output, std::string& error) override
        try {
        if (!module.spirv.empty()) {
            output = module.spirv;
            error.clear();
            return true;
        }
        if (!available) {
            error = "runtime_spirv_compiler_unavailable";
            return false;
        }
        if (module.hlsl.empty() || module.hlsl.size() > 8 * 1024 * 1024 ||
            module.hlsl.find('\0') != std::string::npos || module.entry.empty() ||
            module.entry.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                std::string::npos ||
            (module.stage != ShaderStage::Vertex && module.stage != ShaderStage::Pixel)) {
            error = "invalid_shader_source";
            return false;
        }
        static const std::regex includeDirective(R"((^|\n)[ \t]*#[ \t]*include\b)");
        if (std::regex_search(module.hlsl, includeDirective)) {
            error = "shader_includes_must_be_expanded";
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        const std::string key =
            std::to_string(unsigned(module.stage)) + ":" + module.entry + ":" + module.hlsl;
        auto found = cache.find(key);
        if (found != cache.end()) {
            output = found->second;
            ++stats.cacheHits;
            error.clear();
            return true;
        }
        Workspace workspace;
        const auto source = workspace.path / "source.hlsl", object = workspace.path / "module.spv";
        {
            std::ofstream file(source, std::ios::binary);
            file.write(module.hlsl.data(), module.hlsl.size());
            if (!file) {
                error = "shader_source_write_failed";
                return false;
            }
        }
        std::vector<std::string> args{"-spirv",
                                      "-fspv-target-env=vulkan1.3",
                                      "-fvk-use-dx-layout",
                                      "-fvk-stage-io-order=decl",
                                      "-fvk-b-shift",
                                      "0",
                                      "0",
                                      "-fvk-t-shift",
                                      "4",
                                      "0",
                                      "-fvk-s-shift",
                                      "20",
                                      "0",
                                      "-Zpr",
                                      "-Ges",
                                      "-Gis",
                                      "-T",
                                      module.stage == ShaderStage::Vertex ? "vs_6_0" : "ps_6_0",
                                      "-E",
                                      module.entry,
                                      "-Fo",
                                      object.u8string(),
                                      source.u8string()};
        ++stats.compilations;
        if (!Run(options.executable, args, workspace, options.timeoutMilliseconds, error))
            return false;
        const auto data = Read(object, 16 * 1024 * 1024 + 1);
        if (data.size() < 20 || data.size() > 16 * 1024 * 1024 || data.size() % 4 ||
            static_cast<unsigned char>(data[0]) != 3 || static_cast<unsigned char>(data[1]) != 2 ||
            static_cast<unsigned char>(data[2]) != 35 || static_cast<unsigned char>(data[3]) != 7) {
            error = "missing_or_invalid_spirv_object";
            return false;
        }
        SnapshotBytes bytes;
        bytes.assign(data.begin(), data.end());
        bytes.Seal();
        const auto cost = bytes.size() + key.size();
        if (cost <= options.cacheBytes) {
            while (!cache.empty() && cachedBytes + cost > options.cacheBytes) {
                cachedBytes -= cache.begin()->first.size() + cache.begin()->second.size();
                cache.erase(cache.begin());
            }
            cache.emplace(key, bytes);
            cachedBytes += cost;
        }
        output = std::move(bytes);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
};
fs::path ExecutableDirectory() {
#ifdef _WIN32
    std::wstring path(32768, 0);
    const auto length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!length || length >= path.size())
        return {};
    path.resize(length);
    return fs::path(path).parent_path();
#elif defined(__APPLE__)
    std::uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::string path(length, 0);
    if (_NSGetExecutablePath(path.data(), &length))
        return {};
    return fs::path(path.c_str()).parent_path();
#else
    std::string path(32768, 0);
    const auto length = readlink("/proc/self/exe", path.data(), path.size());
    if (length <= 0 || std::size_t(length) == path.size())
        return {};
    path.resize(std::size_t(length));
    return fs::path(path).parent_path();
#endif
}
} // namespace
std::shared_ptr<SpirvCompiler> CreateDxcSpirvCompiler(ShaderCompilerOptions options) {
    return std::make_shared<DxcCompiler>(std::move(options));
}
std::shared_ptr<SpirvCompiler> DefaultSpirvCompiler() {
    static const auto compiler = [] {
#ifdef _WIN32
        const auto filename = "dxc.exe";
#else
        const auto filename = "dxc";
#endif
        auto candidate = ExecutableDirectory() / filename;
#ifdef MDS_DXC_EXECUTABLE
        if (!fs::is_regular_file(candidate))
            candidate = fs::u8path(MDS_DXC_EXECUTABLE);
#endif
        return CreateDxcSpirvCompiler({candidate});
    }();
    return compiler;
}
} // namespace mikudancestudio::rhi
