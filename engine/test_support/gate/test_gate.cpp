// alamo_test_gate: runs one optional-suite CTest entry (retail game data, Vulkan, retail traces) only when its
// prerequisites are present, so a run without them reports the entry as skipped with a reason instead of passing it.
//
//   alamo_test_gate --suite <retail|vulkan|traces|...> --needs <need>[,<need>...] [--] <command> [args...]
//
// Needs:
//   foc          ALAMO_FOC_DATA names a folder with a Data\ subfolder (the Forces of Corruption install)
//   eaw          ALAMO_EAW_DATA names a folder with a Data\ subfolder (the base game's GameData)
//   traces       ALAMO_Q2_TRACES names an existing folder (retail trace captures)
//   vulkan       vulkan-1.dll loads and reports a Vulkan 1.3 physical device
//   file:<path>  <path> exists (e.g. extracted/foc_data_text.zip, read at configure time)
//
// Exit codes: 77 (kSkipReturnCode, registered as SKIP_RETURN_CODE) when a need is missing or the test itself printed a
// "SKIP ..." line for this entry's suite; the command's own code otherwise. When ALAMO_TEST_REQUIRE lists the entry's
// suite (comma separated, or "all"), a missing need or a skipped part fails the entry (exit 1) instead: a run that
// declares a suite required must not go green without it.
//
// Every skip prints one line "SKIP-PREREQ <suite> <need>: <reason>" or "SKIP-PART <suite>: <test's line>", which
// tools/test_report.py collects into the per-suite report.
//
// Test-only tool: never linked into engine code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef ALAMO_GATE_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace {

constexpr int kSkip = 77;

std::string env(const char* name) {
    char* v = nullptr;
    size_t len = 0;
    std::string out;
    if (_dupenv_s(&v, &len, name) == 0 && v) {
        out = v;
        std::free(v);
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t end = std::min(s.find(sep, start), s.size());
        if (end > start) out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
}

std::filesystem::path utf8_path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// Empty when the need is met, else the reason it is not.
std::string data_root_missing(const char* var) {
    const std::string root = env(var);
    if (root.empty()) return std::string(var) + " not set";
    std::error_code ec;
    if (!std::filesystem::is_directory(utf8_path(root) / "Data", ec))
        return std::string(var) + " has no Data folder: " + root;
    return {};
}

std::string vulkan_missing() {
#ifndef ALAMO_GATE_HAS_VULKAN
    return "built without Vulkan headers (run toolchain\\3_renderer_deps.cmd)";
#else
    HMODULE lib = LoadLibraryW(L"vulkan-1.dll");
    if (!lib) return "vulkan-1.dll not found (no Vulkan loader)";
    std::string reason;
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(GetProcAddress(lib, "vkGetInstanceProcAddr")));
    auto create = gipa ? reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance")) : nullptr;
    if (!create) {
        reason = "vulkan-1.dll has no vkCreateInstance";
    } else {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "alamo_test_gate";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        VkInstance inst = VK_NULL_HANDLE;
        const VkResult r = create(&ci, nullptr, &inst);
        if (r != VK_SUCCESS) {
            reason = "vkCreateInstance failed (" + std::to_string(static_cast<int>(r)) + "): no Vulkan driver";
        } else {
            auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(inst, "vkEnumeratePhysicalDevices"));
            auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(inst, "vkGetPhysicalDeviceProperties"));
            auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(inst, "vkDestroyInstance"));
            uint32_t n = 0;
            std::vector<VkPhysicalDevice> devs;
            if (enumerate && enumerate(inst, &n, nullptr) == VK_SUCCESS && n) {
                devs.resize(n);
                enumerate(inst, &n, devs.data());
            }
            bool ok = false;
            for (VkPhysicalDevice d : devs) {
                VkPhysicalDeviceProperties p{};
                if (props) props(d, &p);
                ok = ok || p.apiVersion >= VK_API_VERSION_1_3;
            }
            if (!ok) reason = devs.empty() ? "no Vulkan physical device" : "no Vulkan 1.3 physical device";
            if (destroy) destroy(inst, nullptr);
        }
    }
    FreeLibrary(lib);
    return reason;
#endif
}

std::string need_missing(const std::string& need) {
    if (need == "foc") return data_root_missing("ALAMO_FOC_DATA");
    if (need == "eaw") return data_root_missing("ALAMO_EAW_DATA");
    if (need == "traces") {
        const std::string t = env("ALAMO_Q2_TRACES");
        std::error_code ec;
        if (t.empty()) return "ALAMO_Q2_TRACES not set";
        if (!std::filesystem::is_directory(utf8_path(t), ec)) return "ALAMO_Q2_TRACES is not a folder: " + t;
        return {};
    }
    if (need == "vulkan") return vulkan_missing();
    if (need.rfind("file:", 0) == 0) {
        const std::string p = need.substr(5);
        std::error_code ec;
        return std::filesystem::exists(utf8_path(p), ec) ? std::string() : "missing " + p;
    }
    return "unknown need '" + need + "'";
}

// A combined entry ("retail+vulkan") is required only when every one of its suites is: a run that requires the retail
// suite on a machine without a GPU must not fail on the entries that also need a GPU.
bool suite_required(const std::string& suite) {
    const std::vector<std::string> req = split(lower(env("ALAMO_TEST_REQUIRE")), ',');
    for (const std::string& s : req)
        if (s == "all") return true;
    for (const std::string& part : split(suite, '+')) {
        bool found = false;
        for (const std::string& s : req) found = found || s == part;
        if (!found) return false;
    }
    return true;
}

// Windows command-line quoting for one argument (CommandLineToArgvW rules).
void append_quoted(std::wstring& cmd, const std::wstring& arg) {
    if (!cmd.empty()) cmd += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') cmd.append(backslashes * 2 + 1, L'\\');
        else cmd.append(backslashes, L'\\');
        backslashes = 0;
        cmd += c;
    }
    cmd.append(backslashes * 2, L'\\');
    cmd += L'"';
}

// A line the test printed for a part it did not run. Lines from the test's other suites (e.g. "SKIP gpu" inside a
// retail entry, which runs with drivers hidden) belong to their own entries and are ignored.
bool skip_line_for_suite(const std::string& line, const std::string& suite) {
    if (line.rfind("SKIP", 0) != 0 || line.rfind("SKIP-", 0) == 0) return false;
    const std::string l = lower(line);
    const bool gpu = l.find("gpu") != std::string::npos || l.find("vulkan") != std::string::npos ||
                     l.find("dxc") != std::string::npos;
    const bool data = l.find("alamo_foc_data") != std::string::npos || l.find("alamo_eaw_data") != std::string::npos ||
                      l.find("data") != std::string::npos || l.find("install") != std::string::npos;
    const bool traces = l.find("alamo_q2_traces") != std::string::npos || l.find("trace") != std::string::npos;
    bool hit = false;
    for (const std::string& part : split(suite, '+')) {
        if (part == "vulkan") hit = hit || gpu;
        else if (part == "retail") hit = hit || (data && !gpu);
        else if (part == "traces") hit = hit || traces;
    }
    return hit;
}

int usage() {
    std::fprintf(stderr, "usage: alamo_test_gate --suite <name> --needs <need>[,<need>...] [--] <command> [args...]\n");
    return 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    auto narrow = [](const wchar_t* w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    };
    std::string suite;
    std::vector<std::string> needs;
    int i = 1;
    for (; i < argc; ++i) {
        const std::string a = narrow(argv[i]);
        if (a == "--suite" && i + 1 < argc) suite = lower(narrow(argv[++i]));
        else if (a == "--needs" && i + 1 < argc) needs = split(narrow(argv[++i]), ',');
        else if (a == "--") {
            ++i;
            break;
        } else break;
    }
    if (suite.empty() || i >= argc) return usage();
    const bool required = suite_required(suite);

    std::vector<std::string> missing;
    for (const std::string& n : needs) {
        const std::string why = need_missing(n);
        if (!why.empty()) {
            std::printf("SKIP-PREREQ %s %s: %s\n", suite.c_str(), n.c_str(), why.c_str());
            missing.push_back(n);
        }
    }
    if (!missing.empty()) {
        if (required) {
            std::printf("FAIL: suite '%s' is required in this run (ALAMO_TEST_REQUIRE=%s) but its prerequisites are missing\n",
                        suite.c_str(), env("ALAMO_TEST_REQUIRE").c_str());
            return 1;
        }
        std::fflush(stdout);
        return kSkip;
    }

    std::wstring cmd;
    for (int k = i; k < argc; ++k) append_quoted(cmd, argv[k]);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return usage();
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        std::printf("FAIL: could not start %s (error %lu)\n", narrow(argv[i]).c_str(), GetLastError());
        return 1;
    }
    CloseHandle(wr);
    std::vector<std::string> skipped;
    std::string line;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got) {
        std::fwrite(buf, 1, got, stdout);
        for (DWORD k = 0; k < got; ++k) {
            if (buf[k] == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (skip_line_for_suite(line, suite)) skipped.push_back(line);
                line.clear();
            } else {
                line += buf[k];
            }
        }
    }
    if (skip_line_for_suite(line, suite)) skipped.push_back(line);
    std::fflush(stdout);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);

    const bool child_skipped = code == static_cast<DWORD>(kSkip);
    if (code != 0 && !child_skipped) return static_cast<int>(code);
    if (!child_skipped && skipped.empty()) return 0;
    for (const std::string& s : skipped) std::printf("SKIP-PART %s: %s\n", suite.c_str(), s.c_str());
    if (child_skipped && skipped.empty()) std::printf("SKIP-PART %s: the test exited with the skip code\n", suite.c_str());
    if (required) {
        std::printf("FAIL: suite '%s' is required in this run but the test skipped part of it\n", suite.c_str());
        return 1;
    }
    return kSkip;
}
