#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <psapi.h>
#include <time.h>

#define SERVICE_NAME "fuck_discord"

SERVICE_STATUS g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle;
HANDLE g_ServiceStopEvent = NULL;

FILE* logger = NULL;

void log_init() {
    #ifdef DEBUG
    logger = fopen("C:\\fuck_discord_service_logs.txt", "a+");
    if (logger) {
        fprintf(logger, "=== Service started ===\n");
        fflush(logger);
    }
    #endif
}

void log_msg(const char* fmt, ...) {
    #ifdef DEBUG
    if (!logger) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(logger, fmt, args);
    fprintf(logger, "\n");
    fflush(logger);
    va_end(args);
    #endif
}

const char *priorityToString(DWORD p)
{
    switch (p)
    {
    case NORMAL_PRIORITY_CLASS:
        return "Normal";
    case IDLE_PRIORITY_CLASS:
        return "Idle";
    case HIGH_PRIORITY_CLASS:
        return "High";
    case REALTIME_PRIORITY_CLASS:
        return "Realtime";
    case BELOW_NORMAL_PRIORITY_CLASS:
        return "Below normal";
    case ABOVE_NORMAL_PRIORITY_CLASS:
        return "Above normal";

    default:
        return "unknown";
    }
}

const DWORD AboveNormalPriorities = ABOVE_NORMAL_PRIORITY_CLASS | HIGH_PRIORITY_CLASS | REALTIME_PRIORITY_CLASS;

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam){
    for (;;){
        DWORD processes[1024], cbNeeded, processes_count;
        if (!EnumProcesses(processes, sizeof(processes), &cbNeeded)){
            log_msg("Failed to enumerate processes\n");
            return 1;
        }

        processes_count = cbNeeded / sizeof(DWORD);

        for (DWORD i = 0; i < processes_count; i++){
            HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processes[i]);
            if (hProcess != NULL){
                char szProcessName[MAX_PATH] = "";
                HMODULE hMod;
                DWORD cbNeeded;
                if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)){
                    GetModuleBaseNameA(hProcess, hMod, szProcessName, sizeof(szProcessName) / sizeof(char));
                }

                if (strcmp(szProcessName, "Discord.exe") == 0){
                    DWORD priority = GetPriorityClass(hProcess);
                    if (!(priority & AboveNormalPriorities)){
                        CloseHandle(hProcess);
                        continue;
                    }

                    time_t t = time(NULL);
                    struct tm tm = *localtime(&t);
                    log_msg("[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min, tm.tm_sec);

                    if (SetPriorityClass(hProcess, NORMAL_PRIORITY_CLASS)){
                        log_msg("Discord.exe (%ld) %s -> %s\n", processes[i], priorityToString(priority), priorityToString(NORMAL_PRIORITY_CLASS));
                    }
                    else{
                        log_msg("Failed to set priority of Discord.exe (%ld) to normal\n", processes[i]);
                    }
                }

                CloseHandle(hProcess);
            }
        }
        Sleep(5000);
    }
}

void WINAPI ServiceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
        case SERVICE_CONTROL_STOP:
            if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
                return;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            SetEvent(g_ServiceStopEvent);
            break;
        default:
            break;
    }
}

void WINAPI ServiceMain(DWORD argc, LPTSTR* argv) {
    log_init();

    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

int main() {
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        { SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
        { NULL, NULL }
    };
    StartServiceCtrlDispatcher(ServiceTable);
    return 0;
}