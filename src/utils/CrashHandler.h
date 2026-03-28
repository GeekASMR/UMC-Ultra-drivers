#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <string>
#include <ctime>
#include <cstdio>
#include "Logger.h"

// Require linking against dbghelp.lib for MiniDumpWriteDump
#pragma comment(lib, "Dbghelp.lib")

class CrashHandler {
public:
    static void init() {
        if (!s_initialized) {
            // Register Vectored Exception Handler (VEH)
            // 1 = Call first, before SEH blocks
            s_vehHandle = AddVectoredExceptionHandler(1, VectoredHandler);
            s_initialized = true;
            LOG_INFO("CrashHandler", "VEH (Vectored Exception Handler) registered for Crash Dumps.");
        }
    }

    static void uninit() {
        if (s_initialized && s_vehHandle) {
            RemoveVectoredExceptionHandler(s_vehHandle);
            s_vehHandle = nullptr;
            s_initialized = false;
            LOG_INFO("CrashHandler", "VEH unregistered.");
        }
    }

private:
    static inline bool s_initialized = false;
    static inline PVOID s_vehHandle = nullptr;

    static LONG WINAPI VectoredHandler(struct _EXCEPTION_POINTERS* ExceptionInfo) {
        DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;
        
        // Trap only critical, non-continuable fault codes (mostly memory/math errors)
        if (code == EXCEPTION_ACCESS_VIOLATION || 
            code == EXCEPTION_ILLEGAL_INSTRUCTION || 
            code == EXCEPTION_INT_DIVIDE_BY_ZERO || 
            code == EXCEPTION_STACK_OVERFLOW ||
            code == EXCEPTION_PRIV_INSTRUCTION ||
            code == EXCEPTION_IN_PAGE_ERROR) {
            
            LOG_ERROR("CrashHandler", "FATAL: Vectored Exception %08X caught! Generating MiniDump...", code);
            GenerateDump(ExceptionInfo);
            
            // Pass it to the host DAW so it knows it crashed and can close, we don't swallow it.
            return EXCEPTION_CONTINUE_SEARCH;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static void GenerateDump(EXCEPTION_POINTERS* pExceptionPointers) {
        char dumpPath[MAX_PATH];
        time_t t = time(nullptr);
        struct tm* tm = localtime(&t);
        sprintf(dumpPath, "C:\\Users\\Public\\Documents\\UMCUltra_Crash_%04d%02d%02d_%02d%02d%02d.dmp",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec);

        HANDLE hFile = CreateFileA(dumpPath, GENERIC_READ | GENERIC_WRITE, 
            0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mdei;
            mdei.ThreadId           = GetCurrentThreadId();
            mdei.ExceptionPointers  = pExceptionPointers;
            mdei.ClientPointers     = FALSE;

            MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs);

            BOOL rv = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                        hFile, mdt, (pExceptionPointers != 0) ? &mdei : 0,
                                        0, 0);
            CloseHandle(hFile);

            if (rv) {
                LOG_INFO("CrashHandler", "MiniDump saved successfully to %s", dumpPath);
            } else {
                LOG_ERROR("CrashHandler", "MiniDumpWriteDump failed. Error: %u", GetLastError());
            }
        } else {
            LOG_ERROR("CrashHandler", "Failed to create dump file: %s", dumpPath);
        }
    }
};
