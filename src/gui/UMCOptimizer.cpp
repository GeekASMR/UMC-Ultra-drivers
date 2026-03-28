#include <windows.h>
#include "OptimizerManager.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Make sure we have the required DLLs loaded
    LoadLibraryW(L"uxtheme.dll");
    LoadLibraryW(L"comctl32.dll");
    LoadLibraryW(L"dwmapi.dll");

    // Start UI
    OptimizerManager::ShowDialog(NULL);

    return 0;
}
