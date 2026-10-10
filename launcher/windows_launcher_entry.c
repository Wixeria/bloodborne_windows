/* SPDX-License-Identifier: GPL-2.0-or-later
 * GUI entry point: locate this checkout and launch its Tk frontend without a console.
 * Python remains the same dependency used by the game's preparation scripts.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>

static int regular_file(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, wchar_t *cmd, int show) {
    (void)instance; (void)previous; (void)cmd; (void)show;
    wchar_t root[32768], script[32768], check[32768], python[32768], arguments[32768];
    if (!GetModuleFileNameW(NULL, root, 32768)) return 1;
    int found = 0;
    for (int n = 0; n < 8; ++n) {
        wchar_t *slash = wcsrchr(root, L'\\');
        if (!slash) break;
        *slash = 0;
        if (swprintf(check, 32768, L"%ls\\run-windows.ps1", root) < 0) break;
        if (swprintf(script, 32768, L"%ls\\launcher\\windows_launcher.py", root) < 0) break;
        if (regular_file(check) && regular_file(script)) { found = 1; break; }
    }
    if (!found) {
        MessageBoxW(NULL, L"Keep this launcher in the bbport checkout (or out\\windows-launcher).",
                    L"Bloodborne Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
    int pyw = 0;
    DWORD len = SearchPathW(NULL, L"pythonw.exe", NULL, 32768, python, NULL);
    if (!len || len >= 32768) {
        len = SearchPathW(NULL, L"pyw.exe", NULL, 32768, python, NULL);
        pyw = 1;
    }
    if (!len || len >= 32768 || !regular_file(python)) {
        MessageBoxW(NULL, L"Install Python 3.10+ with Tk and add it to PATH, then reopen the launcher.",
                    L"Bloodborne Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (swprintf(arguments, 32768, pyw ? L"-3 \"%ls\"" : L"\"%ls\"", script) < 0) return 1;
    SHELLEXECUTEINFOW launch = {0};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.lpFile = python;
    launch.lpParameters = arguments;
    launch.lpDirectory = root;
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch)) {
        MessageBoxW(NULL, L"Could not start Python. Run python launcher\\windows_launcher.py for details.",
                    L"Bloodborne Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (launch.hProcess) CloseHandle(launch.hProcess);
    return 0;
}
