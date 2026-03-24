#include "ControlPanelIPC.h"
#include "../utils/Logger.h"

ControlPanelIPC::ControlPanelIPC() : m_hMap(NULL), m_pState(nullptr) {}

ControlPanelIPC::~ControlPanelIPC() { close(); }

bool ControlPanelIPC::init() {
    m_hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(TotalMixIPC), IPC_MAP_NAME);
    if (!m_hMap) return false;

    bool exists = (GetLastError() == ERROR_ALREADY_EXISTS);
    m_pState = (TotalMixIPC*)MapViewOfFile(m_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(TotalMixIPC));
    
    if (!m_pState) {
        CloseHandle(m_hMap);
        m_hMap = NULL;
        return false;
    }

    if (!exists && !m_pState->initialized) {
        // First creation, zero out
        memset(m_pState, 0, sizeof(TotalMixIPC));
        for(int s=0; s<MAX_SRC; s++) {
            for(int d=0; d<MAX_DST; d++) {
                m_pState->matrixGain[s][d] = 1.0f; // Default unity gain
            }
        }
        m_pState->initialized = true;
    }

    return true;
}

void ControlPanelIPC::close() {
    if (m_pState) { UnmapViewOfFile(m_pState); m_pState = nullptr; }
    if (m_hMap) { CloseHandle(m_hMap); m_hMap = NULL; }
}
