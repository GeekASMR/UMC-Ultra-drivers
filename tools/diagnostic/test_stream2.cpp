#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")

static const GUID G = {0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

bool io(HANDLE h, DWORD code, void* in, DWORD inSz, void* out=NULL, DWORD outSz=0) {
    DWORD br; return DeviceIoControl(h, code, in, inSz, out, outSz, &br, NULL) != FALSE;
}

int main() {
    printf("=== Stream Test v2 - Polling DMA ===\n\n");
    
    HDEVINFO di = SetupDiGetClassDevsW(&G, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id = {}; id.cbSize = sizeof(id);
    SetupDiEnumDeviceInterfaces(di, NULL, &G, 0, &id);
    DWORD rs; SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &rs, NULL);
    auto* dt = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1,rs);
    dt->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di, &id, dt, rs, NULL, NULL);
    HANDLE h = CreateFileW(dt->DevicePath, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    free(dt); SetupDiDestroyDeviceInfoList(di);
    
    BYTE tmp[1040]; DWORD br;
    io(h, 0x80882004, NULL, 0, tmp, 24);
    io(h, 0x80882820, NULL, 0, tmp, 4);
    io(h, 0x808820C4, NULL, 0, tmp, 1040);
    DWORD mode = 0; io(h, 0x80882804, &mode, 4);
    
    // Allocate a large control block (like official DLL)
    // Official: control at 0x960000, first DMA at 0x970000
    // So control block is 64KB, and offsets 0x17C/0x180/0x184 are within it
    BYTE* mem = (BYTE*)VirtualAlloc(NULL, 40 * 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    BYTE* ctrl = mem;           // control page
    BYTE* dma = mem + 0x10000;  // DMA starts here
    
    printf("ctrl=%p, dma=%p\n", ctrl, dma);
    
    // SET_CALLBACKS with offsets (not event handles)
    // Mimic exact pattern: addr, then 3 offsets within that addr
    BYTE cb[32] = {};
    *(UINT64*)&cb[0]  = (UINT64)(uintptr_t)ctrl;
    // Use same relative offsets as official: 0x17C, 0x180, 0x184
    *(UINT64*)&cb[8]  = 0x17C;
    *(UINT64*)&cb[16] = 0x180;
    *(UINT64*)&cb[24] = 0x184;
    
    printf("SET_CALLBACKS: ctrl=%p, offsets=0x17C,0x180,0x184\n", ctrl);
    io(h, 0x80882880, cb, 32);
    
    // Mark control page offsets so we can detect writes
    *(DWORD*)(ctrl + 0x17C) = 0xDEADBEEF;
    *(DWORD*)(ctrl + 0x180) = 0xDEADBEEF;
    *(DWORD*)(ctrl + 0x184) = 0xDEADBEEF;
    
    BYTE config[292]; io(h, 0x80882808, NULL, 0, config, 292);
    BYTE chList[8200] = {}; io(h, 0x8088280C, NULL, 0, chList, 8200);
    DWORD numIn = *(DWORD*)&chList[0];
    DWORD numOut = *(DWORD*)&chList[4100];
    
    // SET_BUFFER_SIZE
    DWORD bufSz = 128; io(h, 0x80882824, &bufSz, 4);
    
    // SELECT + MAP all input and output channels
    int chIdx = 0;
    auto setupChannels = [&](DWORD blockOff, DWORD num) {
        for (DWORD i = 0; i < num; i++) {
            DWORD off = blockOff + 4 + i * 16;
            io(h, 0x80882840, &chList[off], 16); // SELECT
            
            DWORD chId = *(DWORD*)&chList[off+8];
            DWORD type = *(DWORD*)&chList[off+12];
            BYTE mapBuf[24] = {};
            *(DWORD*)&mapBuf[0] = chId;
            *(DWORD*)&mapBuf[4] = type;
            *(DWORD*)&mapBuf[8] = bufSz * 4;
            *(DWORD*)&mapBuf[12] = 32;
            *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)(dma + chIdx * 0x10000);
            io(h, 0x808828A0, mapBuf, 24);
            chIdx++;
        }
    };
    
    setupChannels(0, numIn);
    setupChannels(4100, numOut);
    printf("Mapped %d channels\n", chIdx);
    
    // START
    io(h, 0x808828C8, NULL, 0);
    printf("Started!\n");
    
    // Poll control page and DMA for 3 seconds
    DWORD prev17C = 0xDEADBEEF;
    int changes = 0;
    DWORD t0 = GetTickCount();
    
    while (GetTickCount() - t0 < 3000) {
        DWORD cur = *(volatile DWORD*)(ctrl + 0x17C);
        if (cur != prev17C) {
            changes++;
            if (changes <= 5) {
                printf("  [%4ums] ctrl+0x17C changed: 0x%08X -> 0x%08X\n",
                       GetTickCount()-t0, prev17C, cur);
                printf("    ctrl+0x180=0x%08X ctrl+0x184=0x%08X\n",
                       *(DWORD*)(ctrl+0x180), *(DWORD*)(ctrl+0x184));
                       
                // Check first input DMA
                float* in0 = (float*)dma;
                printf("    In0 samples: %.4f %.4f %.4f %.4f\n",
                       in0[0], in0[1], in0[2], in0[3]);
            }
            prev17C = cur;
        }
        Sleep(1);
    }
    
    printf("\nControl page changes in 3s: %d (%.1f Hz)\n", changes, changes/3.0);
    
    // Final state
    printf("Final ctrl+0x17C=0x%08X 0x180=0x%08X 0x184=0x%08X\n",
           *(DWORD*)(ctrl+0x17C), *(DWORD*)(ctrl+0x180), *(DWORD*)(ctrl+0x184));
    
    // Scan entire control page for any non-zero data
    printf("\nNon-zero regions in control page:\n");
    for (int i = 0; i < 4096; i += 4) {
        DWORD v = *(DWORD*)(ctrl + i);
        if (v != 0 && v != 0xDEADBEEF) {
            printf("  offset 0x%03X: 0x%08X\n", i, v);
        }
    }
    
    // Check any DMA buffer for data
    printf("\nChecking DMA buffers for non-zero data:\n");
    for (int c = 0; c < chIdx && c < 4; c++) {
        float* buf = (float*)(dma + c * 0x10000);
        bool hasData = false;
        for (int s = 0; s < 128; s++) {
            if (buf[s] != 0.0f) { hasData = true; break; }
        }
        printf("  Ch[%d]: %s\n", c, hasData ? "HAS DATA!" : "empty");
    }
    
    io(h, 0x808828CC, NULL, 0);
    VirtualFree(mem, 0, MEM_RELEASE);
    CloseHandle(h);
    return 0;
}
