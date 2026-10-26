#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

// ============================================================
// 【授权验证模块】
// ============================================================

// ★ 服务器配置（与你的 curl 命令完全一致）
static const std::wstring AUTH_HOST = L"43.142.19.80";              // 公网 IP，不带 http://
static const int          AUTH_PORT = 8080;                         // API 端口
static const std::wstring AUTH_KEY = L"6534465asdasdsa55";         // 与 Go 服务端 API_KEY 一致

// 【新增】宽字符串转 UTF-8
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
        nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
        &s[0], len, nullptr, nullptr);
    return s;
}

// 【新增】FNV-1a 哈希，把字符串压成 16 位十六进制
static std::string HashString(const std::string& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llX", h);
    return std::string(buf);
}

// 【新增】获取本机机器码
static std::string GetMachineCode() {
    DWORD volSerial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &volSerial, nullptr, nullptr, nullptr, 0);

    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(compName, &size);

    std::ostringstream oss;
    oss << WideToUtf8(compName) << "-" << std::hex << std::uppercase << volSerial;

    std::string h = HashString(oss.str());
    std::string code;
    for (int i = 0; i < 16; i += 4) {
        if (i) code += "-";
        code += h.substr(i, 4);
    }
    return code;
}

// 【新增】复制字符串到系统剪贴板
static void CopyToClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pMem, wlen);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}

// 【修复】发送 HTTP GET 请求，增加端口参数
static std::string HttpGet(const std::wstring& host, int port, const std::wstring& path) {
    std::string result;

    HINTERNET hSession = WinHttpOpen(L"ClickTool/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    // ★ 用传入的端口，而不是默认 80
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (hRequest) {
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD avail = 0;
            do {
                avail = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
                std::vector<char> buf(avail + 1, 0);
                DWORD read = 0;
                WinHttpReadData(hRequest, buf.data(), avail, &read);
                result.append(buf.data(), read);
            } while (true);
        }
        WinHttpCloseHandle(hRequest);
    }
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// 【新增】授权验证主流程
static bool VerifyLicense() {
    // 1. 生成机器码并显示
    std::string mc = GetMachineCode();
    std::cout << "[授权] 本机机器码: " << mc << std::endl;

    // 2. 自动复制到剪贴板
    CopyToClipboard(mc);
    std::cout << "[授权] 已复制到剪贴板，可直接粘贴发送给管理员。" << std::endl;

    // 3. 请求服务器（端口用 8080）
    std::wstring path = L"/check?key=" + AUTH_KEY + L"&machine_code=" +
        std::wstring(mc.begin(), mc.end());
    std::string resp = HttpGet(AUTH_HOST, AUTH_PORT, path);

    // 4. 去掉首尾空白
    while (!resp.empty() && (resp.back() == '\r' || resp.back() == '\n' ||
        resp.back() == ' ' || resp.back() == '\t'))
        resp.pop_back();

    // 5. 判定结果
    if (resp.empty()) {
        std::cout << "[授权] 无法连接授权服务器，程序退出。" << std::endl;
        return false;
    }
    if (resp != "1") {
        std::cout << "[授权] 本机未授权或已被禁用，请联系管理员。" << std::endl;

        // 顺手把申请文案也复制好
        CopyToClipboard("申请授权，机器码：" + mc);
        std::cout << "[授权] 申请信息已复制到剪贴板，粘贴发送给管理员即可。" << std::endl;
        return false;
    }
    std::cout << "[授权] 验证通过。" << std::endl;
    return true;
}

// ============================================================
// 以下为原有功能代码
// ============================================================

struct EnumData {
    DWORD processId;
    HWND hwnd;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumData& data = *(EnumData*)lParam;
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    if (data.processId == processId && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        data.hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

DWORD GetProcessIdByName(const std::wstring& processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W processEntry;
        processEntry.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(snapshot, &processEntry)) {
            do {
                if (processName == processEntry.szExeFile) {
                    pid = processEntry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &processEntry));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

void ClickClient(HWND hwnd, int x, int y) {
    LPARAM lParam = MAKELPARAM(x, y);
    PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    PostMessage(hwnd, WM_LBUTTONUP, 0, lParam);
}

void Function1_Purchase(HWND hwnd, int delayMs, int loopCount) {
    std::vector<std::pair<int, int>> points = {
        {413, 585},
        {434, 486}
    };

    int count = 0;
    bool infiniteLoop = (loopCount == 0);

    std::cout << "开始执行 [功能1-购买]..." << std::endl;

    while (infiniteLoop || count < loopCount) {
        for (const auto& pt : points) {
            ClickClient(hwnd, pt.first, pt.second);
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        count++;
        std::cout << "运行中，第 " << count << " 次" << std::endl;

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            std::cout << "检测到 ESC 键，已停止运行。" << std::endl;
            break;
        }
    }
    std::cout << "执行完成！" << std::endl;
}

// ============================================================
// 主函数
// ============================================================
int main() {
    // 设置控制台代码页为 UTF-8，避免中文乱码
    SetConsoleOutputCP(CP_UTF8);

    // ---- 启动时先做授权验证 ----
    if (!VerifyLicense()) {
        std::cout << "\n按回车键退出...";
        std::cin.get();
        return 1;
    }

    // ---- 原有逻辑 ----
    std::wstring exeName = L"GameApp.exe";
    int delay = 100;
    int loopCount = 0;

    DWORD pid = GetProcessIdByName(exeName);
    if (pid == 0) {
        std::cerr << "错误：找不到进程 " << WideToUtf8(exeName) << std::endl;
        std::cout << "\n按回车键退出...";
        std::cin.get();
        return 1;
    }

    EnumData enumData = { pid, NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&enumData);

    if (enumData.hwnd == NULL) {
        std::cerr << "错误：进程存在，但找不到对应的窗口句柄" << std::endl;
        std::cout << "\n按回车键退出...";
        std::cin.get();
        return 1;
    }

    std::cout << "成功找到目标窗口！HWND: " << enumData.hwnd << std::endl;

    Function1_Purchase(enumData.hwnd, delay, loopCount);

    return 0;
}