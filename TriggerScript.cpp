#define UNICODE
#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <CommCtrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ctime>
#include <uxtheme.h>
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define ID_ADD_BUTTON 101
#define ID_EDIT_BUTTON 102
#define ID_REMOVE_BUTTON 103
#define ID_TRAY_ICON 1001
#define WM_TRAYICON (WM_USER + 1)

// Dialog control IDs
#define IDC_PROCESS_NAME 201
#define IDC_SCRIPT_PATH 202
#define IDC_BROWSE_BUTTON 203
#define IDC_TRIGGER_COMBO 204
#define IDC_TIME_INPUT 205
#define IDC_STARTUP_CHECK 206
#define IDC_BROWSE_PROCESS 207

struct TriggerConfig {
    enum Type {
        ON_OPEN,
        ON_CLOSE,
        AT_TIME,
        ON_STARTUP
    } type;
    std::string timeStr;  // For AT_TIME trigger
    bool enabled;
};

struct ProcessConfig {
    std::string scriptPath;
    TriggerConfig trigger;
};

class ProcessMonitor {
private:
    std::map<std::string, HANDLE> running_scripts;
    struct ConfigEntry {
        std::string processName;
        ProcessConfig config;
    };
    std::vector<ConfigEntry> config;
    HWND hwnd;
    HWND listView;
    HWND addButton;
    HWND editButton;
    HWND removeButton;
    NOTIFYICONDATA trayIcon;
    static ProcessMonitor* instance;
    std::thread monitorThread;
    bool running;
    std::string editingProcess;
    std::string editingScriptPath;

    // Add custom colors for dark theme
    COLORREF darkBkColor = RGB(32, 32, 32);
    COLORREF darkTextColor = RGB(240, 240, 240);
    COLORREF buttonColor = RGB(0, 0, 0);  // Pure black for buttons
    HBRUSH darkBkBrush = NULL;
    HBRUSH buttonBrush = NULL;

    static LRESULT CALLBACK ListViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        ProcessMonitor* monitor = reinterpret_cast<ProcessMonitor*>(dwRefData);
        switch (msg) {
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLOREDIT:
                SetTextColor((HDC)wParam, monitor->darkTextColor);
                SetBkColor((HDC)wParam, monitor->darkBkColor);
                return (LRESULT)monitor->darkBkBrush;
            case WM_NOTIFY: {
                NMHDR* nmhdr = (NMHDR*)lParam;
                if (nmhdr->code == HDN_BEGINTRACK || nmhdr->code == HDN_ENDTRACK) {
                    return TRUE;  // Prevent column resizing
                }
                break;
            }
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (instance) {
            return instance->HandleMessage(hwnd, uMsg, wParam, lParam);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void AddTrayIcon() {
        trayIcon.cbSize = sizeof(NOTIFYICONDATA);
        trayIcon.hWnd = hwnd;
        trayIcon.uID = ID_TRAY_ICON;
        trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        trayIcon.uCallbackMessage = WM_TRAYICON;
        trayIcon.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        lstrcpynW(trayIcon.szTip, L"Trigger Script", sizeof(trayIcon.szTip)/sizeof(WCHAR));
        Shell_NotifyIconW(NIM_ADD, &trayIcon);
    }

    void RemoveTrayIcon() {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon);
    }

    void ShowAddDialog() {
        if (DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ADD_PROCESS), hwnd, AddDialogProc, (LPARAM)this) == IDOK) {
            SaveConfig();  // Save after adding/editing a process
            UpdateListView();
        }
        // Clear editing state
        editingProcess.clear();
        editingScriptPath.clear();
    }

    static INT_PTR CALLBACK AddDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        static ProcessMonitor* monitor = nullptr;

        switch (uMsg) {
            case WM_INITDIALOG: {
                monitor = (ProcessMonitor*)lParam;
                
                // Center dialog over main window
                RECT rcOwner, rcDlg;
                GetWindowRect(monitor->hwnd, &rcOwner);
                GetWindowRect(hwndDlg, &rcDlg);
                
                int width = rcDlg.right - rcDlg.left;
                int height = rcDlg.bottom - rcDlg.top;
                
                // Calculate center position with vertical offset
                int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - width) / 2;
                int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - height) / 2 + 12; // Changed to 12 pixel offset
                
                SetWindowPos(hwndDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                
                // Set dialog title based on whether we're editing or adding
                if (!monitor->editingScriptPath.empty()) {
                    SetWindowText(hwndDlg, L"Edit");
                } else {
                    SetWindowText(hwndDlg, L"Add");
                }
                
                // Initialize trigger combo box
                HWND combo = GetDlgItem(hwndDlg, IDC_TRIGGER_COMBO);
                SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)L"When process starts");
                SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)L"When process ends");
                SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)L"At specific time");
                SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)L"On system startup");
                
                // Get handles to the input controls
                HWND timeInput = GetDlgItem(hwndDlg, IDC_TIME_INPUT);
                HWND processInput = GetDlgItem(hwndDlg, IDC_PROCESS_NAME);
                
                // If we're editing, populate the dialog with existing values
                if (!monitor->editingScriptPath.empty()) {
                    auto it = std::find_if(monitor->config.begin(), monitor->config.end(),
                        [&](const ConfigEntry& entry) { 
                            return entry.config.scriptPath == monitor->editingScriptPath;
                        });
                    
                    if (it != monitor->config.end()) {
                        const auto& cfg = it->config;
                        
                        // Set script path
                        SetDlgItemText(hwndDlg, IDC_SCRIPT_PATH, 
                            std::wstring(cfg.scriptPath.begin(), cfg.scriptPath.end()).c_str());
                        
                        // Set trigger type and enable/disable fields accordingly
                        SendMessage(combo, CB_SETCURSEL, cfg.trigger.type, 0);
                        
                        bool isTimeTrigger = (cfg.trigger.type == TriggerConfig::AT_TIME);
                        bool isProcessTrigger = (cfg.trigger.type == TriggerConfig::ON_OPEN || 
                                               cfg.trigger.type == TriggerConfig::ON_CLOSE);
                        
                        // Handle time input
                        EnableWindow(timeInput, isTimeTrigger);
                        SendMessage(timeInput, EM_SETREADONLY, !isTimeTrigger, 0);
                        if (isTimeTrigger) {
                            SetDlgItemText(hwndDlg, IDC_TIME_INPUT, 
                                std::wstring(cfg.trigger.timeStr.begin(), cfg.trigger.timeStr.end()).c_str());
                        }
                        
                        // Handle process input
                        EnableWindow(processInput, isProcessTrigger);
                        SendMessage(processInput, EM_SETREADONLY, !isProcessTrigger, 0);
                        if (!it->processName.empty()) {
                            SetDlgItemText(hwndDlg, IDC_PROCESS_NAME, 
                                std::wstring(it->processName.begin(), it->processName.end()).c_str());
                        }
                    }
                } else {
                    // For new entries, set default selection to "When process starts"
                    SendMessage(combo, CB_SETCURSEL, 0, 0);
                    EnableWindow(timeInput, FALSE);
                    SendMessage(timeInput, EM_SETREADONLY, TRUE, 0);
                    EnableWindow(processInput, TRUE);
                    SendMessage(processInput, EM_SETREADONLY, FALSE, 0);
                }

                // Apply dark theme to dialog
                SetClassLongPtr(hwndDlg, GCLP_HBRBACKGROUND, (LONG_PTR)CreateSolidBrush(monitor->darkBkColor));
                EnumChildWindows(hwndDlg, [](HWND hwnd, LPARAM) -> BOOL {
                    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
                    return TRUE;
                }, 0);
                
                return TRUE;
            }

            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN: {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, monitor->darkTextColor);
                SetBkColor(hdc, monitor->darkBkColor);
                SetBkMode(hdc, TRANSPARENT);
                return (INT_PTR)monitor->darkBkBrush;
            }

            case WM_COMMAND: {
                switch (LOWORD(wParam)) {
                    case IDC_BROWSE_PROCESS: {
                        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                        if (snapshot != INVALID_HANDLE_VALUE) {
                            PROCESSENTRY32W pe32;
                            pe32.dwSize = sizeof(pe32);

                            if (Process32FirstW(snapshot, &pe32)) {
                                // Create popup menu
                                HMENU hMenu = CreatePopupMenu();
                                std::vector<std::wstring> processes;
                                int index = 0;

                                do {
                                    processes.push_back(pe32.szExeFile);
                                    InsertMenuW(hMenu, index, MF_BYPOSITION | MF_STRING, 
                                              1000 + index, pe32.szExeFile);
                                    index++;
                                } while (Process32NextW(snapshot, &pe32));

                                // Show context menu
                                POINT pt;
                                GetCursorPos(&pt);
                                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                       pt.x, pt.y, 0, hwndDlg, NULL);

                                if (cmd >= 1000) {
                                    SetDlgItemText(hwndDlg, IDC_PROCESS_NAME, 
                                                 processes[cmd - 1000].c_str());
                                }

                                DestroyMenu(hMenu);
                            }
                            CloseHandle(snapshot);
                        }
                        return TRUE;
                    }

                    case IDC_BROWSE_BUTTON: {
                        OPENFILENAMEW ofn = {0};
                        WCHAR szFile[MAX_PATH] = L"";
                        
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwndDlg;
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = sizeof(szFile);
                        ofn.lpstrFilter = L"All Files\0*.*\0Scripts\0*.bat;*.cmd;*.ps1;*.py;*.exe\0";
                        ofn.nFilterIndex = 2;
                        ofn.lpstrFileTitle = NULL;
                        ofn.nMaxFileTitle = 0;
                        ofn.lpstrInitialDir = NULL;
                        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                        if (GetOpenFileNameW(&ofn)) {
                            SetDlgItemTextW(hwndDlg, IDC_SCRIPT_PATH, szFile);
                        }
                        return TRUE;
                    }

                    case IDC_TRIGGER_COMBO: {
                        if (HIWORD(wParam) == CBN_SELCHANGE) {
                            int sel = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
                            HWND timeInput = GetDlgItem(hwndDlg, IDC_TIME_INPUT);
                            HWND processInput = GetDlgItem(hwndDlg, IDC_PROCESS_NAME);
                            bool isTimeTrigger = (sel == 2);  // "At specific time" is index 2
                            bool isProcessTrigger = (sel == 0 || sel == 1);  // "When process starts" or "When process ends"
                            
                            // Handle time input
                            EnableWindow(timeInput, isTimeTrigger);
                            if (isTimeTrigger) {
                                SendMessage(timeInput, EM_SETREADONLY, FALSE, 0);
                            } else {
                                SendMessage(timeInput, EM_SETREADONLY, TRUE, 0);
                            }
                            
                            // Handle process input
                            EnableWindow(processInput, isProcessTrigger);
                            if (isProcessTrigger) {
                                SendMessage(processInput, EM_SETREADONLY, FALSE, 0);
                            } else {
                                SendMessage(processInput, EM_SETREADONLY, TRUE, 0);
                            }
                            
                            // If it's a time trigger, set the time
                            if (isTimeTrigger) {
                                SetDlgItemText(hwndDlg, IDC_TIME_INPUT, L"");
                            }
                        }
                        return TRUE;
                    }

                    case IDOK: {
                        WCHAR processName[MAX_PATH];
                        WCHAR scriptPath[MAX_PATH];
                        GetDlgItemText(hwndDlg, IDC_PROCESS_NAME, processName, MAX_PATH);
                        GetDlgItemText(hwndDlg, IDC_SCRIPT_PATH, scriptPath, MAX_PATH);

                        // Get trigger configuration
                        HWND combo = GetDlgItem(hwndDlg, IDC_TRIGGER_COMBO);
                        int sel = SendMessage(combo, CB_GETCURSEL, 0, 0);
                        
                        // Validate inputs based on trigger type
                        bool isValid = true;
                        if (sel == TriggerConfig::AT_TIME) {
                            WCHAR timeStr[32];
                            GetDlgItemText(hwndDlg, IDC_TIME_INPUT, timeStr, 32);
                            if (wcslen(timeStr) == 0) {
                                MessageBoxW(hwndDlg, L"Please enter a time for time-based trigger", L"Error", MB_ICONERROR);
                                isValid = false;
                            }
                        } else if (sel == TriggerConfig::ON_OPEN || sel == TriggerConfig::ON_CLOSE) {
                            if (wcslen(processName) == 0) {
                                MessageBoxW(hwndDlg, L"Please enter a process name for process-based trigger", L"Error", MB_ICONERROR);
                                isValid = false;
                            }
                        }

                        if (wcslen(scriptPath) == 0) {
                            MessageBoxW(hwndDlg, L"Please select a script path", L"Error", MB_ICONERROR);
                            isValid = false;
                        }

                        if (isValid) {
                            ProcessConfig cfg;
                            cfg.scriptPath = std::string(scriptPath, scriptPath + wcslen(scriptPath));
                            cfg.trigger.type = (TriggerConfig::Type)sel;
                            
                            if (sel == TriggerConfig::AT_TIME) {
                                WCHAR timeStr[32];
                                GetDlgItemText(hwndDlg, IDC_TIME_INPUT, timeStr, 32);
                                cfg.trigger.timeStr = std::string(timeStr, timeStr + wcslen(timeStr));
                            }
                            
                            cfg.trigger.enabled = true;
                            
                            std::string procName = (sel == TriggerConfig::AT_TIME || sel == TriggerConfig::ON_STARTUP) ? 
                                                 "" : std::string(processName, processName + wcslen(processName));
                            
                            // If editing, remove the old entry first
                            if (!monitor->editingScriptPath.empty()) {
                                monitor->config.erase(
                                    std::remove_if(monitor->config.begin(), monitor->config.end(),
                                        [&](const ConfigEntry& entry) {
                                            return entry.config.scriptPath == monitor->editingScriptPath;
                                        }
                                    ),
                                    monitor->config.end()
                                );
                            }
                            
                            // Add the new/updated entry
                            monitor->config.push_back({procName, cfg});
                            EndDialog(hwndDlg, IDOK);
                        }
                        return TRUE;
                    }

                    case IDCANCEL:
                        EndDialog(hwndDlg, IDCANCEL);
                        return TRUE;
                }
                break;
            }

            case WM_DRAWITEM: {
                DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
                if (dis->CtlType == ODT_BUTTON) {
                    // Fill background with pure black
                    FillRect(dis->hDC, &dis->rcItem, monitor->buttonBrush);
                    
                    // Get button text
                    WCHAR text[256];
                    GetWindowText(dis->hwndItem, text, 256);
                    
                    // Set text color
                    SetTextColor(dis->hDC, monitor->darkTextColor);
                    SetBkMode(dis->hDC, TRANSPARENT);
                    
                    // Draw text centered
                    DrawText(dis->hDC, text, -1, &dis->rcItem, 
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            
                    // Draw white border
                    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                    HPEN hOldPen = (HPEN)SelectObject(dis->hDC, hPen);
                    Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, 
                            dis->rcItem.right, dis->rcItem.bottom);
                    SelectObject(dis->hDC, hOldPen);
                    DeleteObject(hPen);
                    
                    // Draw focus rect if needed
                    if (dis->itemState & ODS_FOCUS) {
                        DrawFocusRect(dis->hDC, &dis->rcItem);
                    }
                    return TRUE;
                }
                return FALSE;
            }
        }
        return FALSE;
    }

    void RemoveSelected() {
        int selectedIndex = ListView_GetNextItem(listView, -1, LVNI_SELECTED);
        if (selectedIndex != -1) {
            WCHAR processName[MAX_PATH];
            ListView_GetItemText(listView, selectedIndex, 2, processName, MAX_PATH);
            std::string process(processName, processName + wcslen(processName));
            
            // Get the trigger type from the second column
            WCHAR triggerStr[MAX_PATH];
            ListView_GetItemText(listView, selectedIndex, 1, triggerStr, MAX_PATH);
            std::string trigger(triggerStr, triggerStr + wcslen(triggerStr));
            
            // Find the config entry
            auto it = std::find_if(config.begin(), config.end(),
                [&](const ConfigEntry& entry) {
                    // For time-based tasks, match by trigger type
                    if (entry.config.trigger.type == TriggerConfig::AT_TIME) {
                        return entry.config.trigger.type == TriggerConfig::AT_TIME;
                    }
                    // For process-based tasks, match by process name
                    return entry.processName == process;
                });
            
            if (it != config.end()) {
                // Stop the script if it's running
                if (running_scripts.find(it->processName) != running_scripts.end()) {
                    stopScript(running_scripts[it->processName]);
                    running_scripts.erase(it->processName);
                }
                
                // Remove from vector
                config.erase(it);
                SaveConfig();  // Save after removing a process
                UpdateListView();
            }
        }
    }

    void SaveConfig() {
        // Get the directory where the executable is located
        WCHAR exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = std::wstring(exePath);
        exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));
        
        // Convert to string for config file path
        std::string configPath = std::string(exeDir.begin(), exeDir.end()) + "\\config.txt";
        
        std::ofstream file(configPath, std::ios::trunc);
        if (file.is_open()) {
            // Add a header comment for readability
            file << "# Process Monitor Configuration\n";
            file << "# Format: process_name = script_path|trigger_type|trigger_time|enabled\n";
            file << "# Trigger types: 0=On Start, 1=On Close, 2=At Time, 3=On Startup\n\n";

            for (const auto& entry : config) {
                const auto& process = entry.processName;
                const auto& cfg = entry.config;
                file << process << " = " 
                     << cfg.scriptPath << "|"
                     << cfg.trigger.type << "|"
                     << cfg.trigger.timeStr << "|"
                     << (cfg.trigger.enabled ? "1" : "0") 
                     << std::endl;
            }
            file.close();
        } else {
            std::wstring errorMsg = L"Failed to save configuration to: " + std::wstring(configPath.begin(), configPath.end());
            MessageBoxW(hwnd, errorMsg.c_str(), L"Error", MB_ICONERROR);
        }
    }

    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_CREATE: {
                // Create ListView with dark theme
                listView = CreateWindowEx(
                    0, WC_LISTVIEW, L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOSORTHEADER | WS_VSCROLL,
                    0, 0, 520, 150,
                    hwnd, (HMENU)1, GetModuleHandle(NULL), NULL
                );

                // Subclass the ListView to prevent column resizing
                SetWindowSubclass(listView, ListViewProc, 0, (DWORD_PTR)this);

                // Apply dark theme to ListView
                SetWindowTheme(listView, L"DarkMode_Explorer", NULL);
                ListView_SetBkColor(listView, darkBkColor);
                ListView_SetTextColor(listView, darkTextColor);
                ListView_SetTextBkColor(listView, darkBkColor);

                // Enable full row select
                ListView_SetExtendedListViewStyle(listView, LVS_EX_FULLROWSELECT);
                
                // Add columns
                LVCOLUMN lvc;
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
                lvc.fmt = LVCFMT_LEFT | LVCFMT_FIXED_WIDTH;  // Left-align text and prevent resizing
                    
                lvc.pszText = (LPWSTR)L"Script Path";
                lvc.cx = 217;  // Fixed width for Script Path
                ListView_InsertColumn(listView, 0, &lvc);
                    
                lvc.pszText = (LPWSTR)L"Trigger";
                lvc.cx = 120;  // Fixed width for Trigger
                ListView_InsertColumn(listView, 1, &lvc);

                lvc.pszText = (LPWSTR)L"Process";
                lvc.cx = 160;  // Fixed width for Process
                ListView_InsertColumn(listView, 2, &lvc);

                // Get the header control and set its background and text colors
                HWND header = ListView_GetHeader(listView);
                if (header) {
                    SetWindowTheme(header, L"", L"");  // Remove theme for custom drawing
                    // Prevent header item dragging and resizing
                    DWORD headerStyle = GetWindowLong(header, GWL_STYLE);
                    headerStyle &= ~HDS_DRAGDROP;  // Prevent dragging
                    headerStyle |= HDS_NOSIZING;   // Prevent resizing
                    SetWindowLong(header, GWL_STYLE, headerStyle);
                }

                // Create buttons with dark theme
                addButton = CreateWindow(
                    L"BUTTON", L"Add",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                    0, 0, 100, 30,
                    hwnd, (HMENU)ID_ADD_BUTTON, GetModuleHandle(NULL), NULL
                );

                CreateWindow(
                    L"BUTTON", L"Edit",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                    0, 0, 100, 30,
                    hwnd, (HMENU)ID_EDIT_BUTTON, GetModuleHandle(NULL), NULL
                );

                removeButton = CreateWindow(
                    L"BUTTON", L"Remove",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                    0, 0, 100, 30,
                    hwnd, (HMENU)ID_REMOVE_BUTTON, GetModuleHandle(NULL), NULL
                );

                // Apply dark theme to buttons
                SetWindowTheme(addButton, L"DarkMode_Explorer", NULL);
                SetWindowTheme(GetDlgItem(hwnd, ID_EDIT_BUTTON), L"DarkMode_Explorer", NULL);
                SetWindowTheme(removeButton, L"DarkMode_Explorer", NULL);

                darkBkBrush = CreateSolidBrush(darkBkColor);
                buttonBrush = CreateSolidBrush(buttonColor);

                UpdateListView();
                AddTrayIcon();
                return 0;
            }

            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN: {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, darkTextColor);
                SetBkColor(hdc, darkBkColor);
                SetBkMode(hdc, TRANSPARENT);
                return (LRESULT)darkBkBrush;
            }

            case WM_DRAWITEM: {
                DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
                if (dis->CtlType == ODT_BUTTON) {
                    // Fill background with pure black
                    FillRect(dis->hDC, &dis->rcItem, buttonBrush);
                    
                    // Get button text
                    WCHAR text[256];
                    GetWindowText(dis->hwndItem, text, 256);
                    
                    // Set text color
                    SetTextColor(dis->hDC, darkTextColor);
                    SetBkMode(dis->hDC, TRANSPARENT);
                    
                    // Draw text centered
                    DrawText(dis->hDC, text, -1, &dis->rcItem, 
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            
                    // Draw focus rect if needed
                    if (dis->itemState & ODS_FOCUS) {
                        DrawFocusRect(dis->hDC, &dis->rcItem);
                    }
                    return TRUE;
                }
                return FALSE;
            }

            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case ID_ADD_BUTTON:
                        ShowAddDialog();
                        return 0;
                    case ID_EDIT_BUTTON: {
                        int selectedIndex = ListView_GetNextItem(listView, -1, LVNI_SELECTED);
                        if (selectedIndex != -1) {
                            // Get script path from the selected item
                            WCHAR scriptPath[MAX_PATH];
                            ListView_GetItemText(listView, selectedIndex, 0, scriptPath, MAX_PATH);
                            
                            // Store script path for editing
                            editingScriptPath = std::string(scriptPath, scriptPath + wcslen(scriptPath));
                            
                            ShowAddDialog();
                        }
                        return 0;
                    }
                    case ID_REMOVE_BUTTON:
                        RemoveSelected();
                        return 0;
                }
                break;

            case WM_TRAYICON:
                if (lParam == WM_LBUTTONUP) {
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                }
                return 0;

            case WM_SIZE:
                if (wParam == SIZE_MINIMIZED) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    RECT rcClient;
                    GetClientRect(hwnd, &rcClient);
                    int buttonWidth = rcClient.right / 3;
                    int buttonHeight = 30;
                    int bottomMargin = 30;

                    // Position the list view
                    SetWindowPos(listView, NULL, 0, 0, 
                               rcClient.right, rcClient.bottom - buttonHeight - 1, 
                               SWP_NOZORDER);

                    // Draw separator line
                    HDC hdc = GetDC(hwnd);
                    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                    MoveToEx(hdc, 0, rcClient.bottom - buttonHeight - 1, NULL);
                    LineTo(hdc, rcClient.right, rcClient.bottom - buttonHeight - 1);
                    SelectObject(hdc, hOldPen);
                    DeleteObject(hPen);

                    // Position the buttons
                    SetWindowPos(GetDlgItem(hwnd, ID_ADD_BUTTON), NULL, 
                               0, rcClient.bottom - buttonHeight, 
                               buttonWidth - 1, buttonHeight, SWP_NOZORDER);
                    SetWindowPos(GetDlgItem(hwnd, ID_EDIT_BUTTON), NULL, 
                               buttonWidth + 1, rcClient.bottom - buttonHeight,
                               buttonWidth - 2, buttonHeight, SWP_NOZORDER);
                    SetWindowPos(GetDlgItem(hwnd, ID_REMOVE_BUTTON), NULL, 
                               buttonWidth * 2 + 2, rcClient.bottom - buttonHeight,
                               buttonWidth - 1, buttonHeight, SWP_NOZORDER);
                }
                return 0;

            case WM_CLOSE:
                // Stop the monitoring thread first
                running = false;
                if (monitorThread.joinable()) {
                    monitorThread.join();
                }
                
                // Clean up all running scripts
                for (const auto& script : running_scripts) {
                    stopScript(script.second);
                }
                running_scripts.clear();
                
                // Save configuration and remove tray icon
                SaveConfig();
                RemoveTrayIcon();
                
                // Clean up resources
                if (darkBkBrush) {
                    DeleteObject(darkBkBrush);
                }
                if (buttonBrush) {
                    DeleteObject(buttonBrush);
                }
                
                // Destroy window and quit
                DestroyWindow(hwnd);
                PostQuitMessage(0);
                return 0;

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                
                // Redraw the separator line during paint
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                MoveToEx(hdc, 0, rcClient.bottom - 31, NULL);
                LineTo(hdc, rcClient.right, rcClient.bottom - 31);
                SelectObject(hdc, hOldPen);
                DeleteObject(hPen);
                
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_DESTROY:
                RemoveTrayIcon();
                cleanup();
                if (darkBkBrush) {
                    DeleteObject(darkBkBrush);
                }
                if (buttonBrush) {
                    DeleteObject(buttonBrush);
                }
                PostQuitMessage(0);
                return 0;

            case WM_NOTIFY: {
                NMHDR* nmhdr = (NMHDR*)lParam;
                if (nmhdr->hwndFrom == ListView_GetHeader(listView)) {
                    // Prevent column resizing
                    if (nmhdr->code == HDN_BEGINTRACK || nmhdr->code == HDN_ENDTRACK) {
                        return TRUE;  // Return TRUE to prevent the default behavior
                    }
                    if (nmhdr->code == NM_CUSTOMDRAW) {
                        LPNMCUSTOMDRAW ncd = (LPNMCUSTOMDRAW)lParam;
                        if (ncd->dwDrawStage == CDDS_PREPAINT) {
                            return CDRF_NOTIFYITEMDRAW;
                        }
                        if (ncd->dwDrawStage == CDDS_ITEMPREPAINT) {
                            // Fill with pure black background
                            FillRect(ncd->hdc, &ncd->rc, buttonBrush);
                            
                            // Get header text
                            WCHAR text[256];
                            HDITEM hdi = {0};
                            hdi.mask = HDI_TEXT;
                            hdi.pszText = text;
                            hdi.cchTextMax = 256;
                            Header_GetItem(ListView_GetHeader(listView), ncd->dwItemSpec, &hdi);
                            
                            // Set pure white text color and transparent background
                            SetTextColor(ncd->hdc, RGB(255, 255, 255));
                            SetBkMode(ncd->hdc, TRANSPARENT);
                            
                            // Draw text with padding
                            RECT textRect = ncd->rc;
                            textRect.left += 5;
                            textRect.right -= 5;
                            DrawText(ncd->hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                            
                            // Draw white separator line
                            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                            HPEN hOldPen = (HPEN)SelectObject(ncd->hdc, hPen);
                            MoveToEx(ncd->hdc, ncd->rc.right - 1, ncd->rc.top, NULL);
                            LineTo(ncd->hdc, ncd->rc.right - 1, ncd->rc.bottom);
                            SelectObject(ncd->hdc, hOldPen);
                            DeleteObject(hPen);
                            
                            return CDRF_SKIPDEFAULT;
                        }
                    }
                }
                break;
            }
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void UpdateListView() {
        ListView_DeleteAllItems(listView);
        int index = 0;
        
        for (const auto& entry : config) {
            const auto& process = entry.processName;
            const auto& cfg = entry.config;
            LVITEM lvi = {0};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = index;
            
            // Add script path first
            std::wstring wscript(cfg.scriptPath.begin(), cfg.scriptPath.end());
            lvi.pszText = (LPWSTR)wscript.c_str();
            ListView_InsertItem(listView, &lvi);
            
            // Add trigger type
            std::string triggerStr;
            switch (cfg.trigger.type) {
                case TriggerConfig::ON_OPEN: triggerStr = "Process start"; break;
                case TriggerConfig::ON_CLOSE: triggerStr = "Process end"; break;
                case TriggerConfig::AT_TIME: triggerStr = "At " + cfg.trigger.timeStr; break;
                case TriggerConfig::ON_STARTUP: triggerStr = "System startup"; break;
            }
            std::wstring wtrigger(triggerStr.begin(), triggerStr.end());
            ListView_SetItemText(listView, index, 1, (LPWSTR)wtrigger.c_str());
            
            // Add process name (or N/A if empty)
            std::wstring wprocess = process.empty() ? L"N/A" : std::wstring(process.begin(), process.end());
            ListView_SetItemText(listView, index, 2, (LPWSTR)wprocess.c_str());
            
            index++;
        }
    }

    bool isProcessRunning(const std::string& processName) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

        PROCESSENTRY32W processEntry;
        processEntry.dwSize = sizeof(processEntry);

        if (!Process32FirstW(snapshot, &processEntry)) {
            CloseHandle(snapshot);
            return false;
        }

        do {
            std::wstring wProcessName(processEntry.szExeFile);
            std::string currentProcess(wProcessName.begin(), wProcessName.end());
            if (_stricmp(currentProcess.c_str(), processName.c_str()) == 0) {
                CloseHandle(snapshot);
                return true;
            }
        } while (Process32NextW(snapshot, &processEntry));

        CloseHandle(snapshot);
        return false;
    }

    HANDLE runScript(const std::string& scriptPath) {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        std::wstring wscriptPath(scriptPath.begin(), scriptPath.end());
        std::wstring cmdLine;

        // Check file extension to determine how to run it
        std::string ext = scriptPath.substr(scriptPath.find_last_of(".") + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "py") {
            // Use pythonw to run without console window
            cmdLine = L"pythonw " + wscriptPath;
        }
        else if (ext == "bat" || ext == "cmd") {
            cmdLine = L"cmd /c " + wscriptPath;
        }
        else if (ext == "ps1") {
            cmdLine = L"powershell -ExecutionPolicy Bypass -File " + wscriptPath;
        }
        else {
            // For other files (including .exe), run directly
            cmdLine = wscriptPath;
        }

        wchar_t* cmd = _wcsdup(cmdLine.c_str());

        if (!CreateProcessW(
            NULL,           // No module name (use command line)
            cmd,           // Command line
            NULL,           // Process handle not inheritable
            NULL,           // Thread handle not inheritable
            FALSE,          // Set handle inheritance to FALSE
            CREATE_NO_WINDOW,  // Hide console window
            NULL,           // Use parent's environment block
            NULL,           // Use parent's starting directory 
            &si,            // Pointer to STARTUPINFO structure
            &pi            // Pointer to PROCESS_INFORMATION structure
        )) {
            std::wcerr << L"CreateProcess failed. Error: " << GetLastError() << std::endl;
            free(cmd);
            return NULL;
        }

        free(cmd);
        CloseHandle(pi.hThread);
        return pi.hProcess;
    }

    void stopScript(HANDLE process) {
        if (process != NULL) {
            TerminateProcess(process, 0);
            CloseHandle(process);
        }
    }

    bool loadConfig(const std::string& configPath) {
        // Get the directory where the executable is located
        WCHAR exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = std::wstring(exePath);
        exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));
        
        // Convert to string for config file path
        std::string fullConfigPath = std::string(exeDir.begin(), exeDir.end()) + "\\config.txt";
        
        std::ifstream file(fullConfigPath);
        if (!file.is_open()) {
            // Create the file if it doesn't exist
            std::ofstream newFile(fullConfigPath);
            if (newFile.is_open()) {
                newFile << "# Process Monitor Configuration\n";
                newFile << "# Format: process_name = script_path|trigger_type|trigger_time|enabled\n";
                newFile << "# Trigger types: 0=On Start, 1=On Close, 2=At Time, 3=On Startup\n\n";
                newFile.close();
                return true;
            } else {
                std::wstring errorMsg = L"Failed to create configuration file at: " + std::wstring(fullConfigPath.begin(), fullConfigPath.end());
                MessageBoxW(hwnd, errorMsg.c_str(), L"Error", MB_ICONERROR);
                return false;
            }
        }

        config.clear(); // Clear existing configuration
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string process = line.substr(0, pos);
                std::string rest = line.substr(pos + 1);
                
                // Trim whitespace
                process.erase(0, process.find_first_not_of(" \t"));
                process.erase(process.find_last_not_of(" \t") + 1);
                rest.erase(0, rest.find_first_not_of(" \t"));
                rest.erase(rest.find_last_not_of(" \t") + 1);

                // Parse the configuration
                std::stringstream ss(rest);
                std::string scriptPath, triggerType, timeStr, enabled;
                std::getline(ss, scriptPath, '|');
                std::getline(ss, triggerType, '|');
                std::getline(ss, timeStr, '|');
                std::getline(ss, enabled, '|');

                ProcessConfig cfg;
                cfg.scriptPath = scriptPath;
                try {
                    cfg.trigger.type = (TriggerConfig::Type)std::stoi(triggerType);
                    cfg.trigger.timeStr = timeStr;
                    cfg.trigger.enabled = (enabled == "1");
                    config.push_back({process, cfg});  // Add to vector in order
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing config for process " << process << ": " << e.what() << std::endl;
                }
            }
        }
        return true;
    }

    bool shouldRunScript(const std::string& processName, bool isRunning) {
        auto it = std::find_if(config.begin(), config.end(),
            [&processName](const ConfigEntry& entry) { return entry.processName == processName; });
        
        if (it == config.end()) {
            return false;
        }
        const auto& cfg = it->config;
        
        bool shouldRun = false;
        switch (cfg.trigger.type) {
            case TriggerConfig::ON_OPEN:
                shouldRun = isRunning && running_scripts.find(processName) == running_scripts.end();
                if (shouldRun) {
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                }
                break;
                
            case TriggerConfig::ON_CLOSE:
                // Only trigger the script if we were tracking this process and it's now closed
                if (!isRunning && running_scripts.find(processName) != running_scripts.end()) {
                    shouldRun = true;
                    // Remove from running scripts map after detecting closure
                    stopScript(running_scripts[processName]);
                    running_scripts.erase(processName);
                }
                break;
                
            case TriggerConfig::AT_TIME: {
                time_t now = time(nullptr);
                struct tm timeinfo;
                localtime_s(&timeinfo, &now);
                char current_time[6];
                strftime(current_time, sizeof(current_time), "%H:%M", &timeinfo);
                shouldRun = std::string(current_time) == cfg.trigger.timeStr;
                break;
            }
                
            case TriggerConfig::ON_STARTUP:
                shouldRun = false;
                break;
        }
        
        return shouldRun;
    }

    void monitor() {
        while (running) {
            try {
                for (const auto& entry : config) {
                    const auto& processName = entry.processName;
                    bool isRunning = isProcessRunning(processName);
                    
                    if (shouldRunScript(processName, isRunning)) {
                        HANDLE scriptHandle = runScript(entry.config.scriptPath);
                        if (scriptHandle) {
                            running_scripts[processName] = scriptHandle;
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            catch (const std::exception& e) {
                std::cerr << "Error in monitor loop: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }

    void cleanup() {
        running = false;
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
        for (const auto& script : running_scripts) {
            stopScript(script.second);
        }
        running_scripts.clear();
        SaveConfig();
    }

public:
    ProcessMonitor(const std::string& configPath = "config.txt") : running(true) {
        instance = this;
        
        // Initialize COM for shell icons
        CoInitialize(NULL);

        // Load configuration first
        if (!loadConfig(configPath)) {
            std::cerr << "Failed to load configuration." << std::endl;
        }

        // Register window class
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"ProcessMonitorClass";
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        
        RegisterClassEx(&wc);

        // Create window
        hwnd = CreateWindowEx(
            0, L"ProcessMonitorClass", L"TriggerScript",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 520, 300,  // Changed from 400 to 300
            NULL, NULL, GetModuleHandle(NULL), NULL
        );

        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }

        // Start monitoring in a separate thread
        monitorThread = std::thread([this]() { this->monitor(); });
    }

    ~ProcessMonitor() {
        cleanup();
        CoUninitialize();
    }

    void run() {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

ProcessMonitor* ProcessMonitor::instance = nullptr;

int main() {
    try {
        ProcessMonitor monitor;
        monitor.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
} 