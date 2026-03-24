#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")

static const GUID G = {0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

HANDLE g_ovEvent = NULL;
OVERLAPPED g_ov = {};

bool io(HANDLE h, DWORD code, void* in, DWORD inSz, void* out=NULL, DWORD outSz=0, DWORD* pRet=NULL) {
    memset(&g_ov, 0, sizeof(g_ov));
    g_ov.hEvent = g_ovEvent;
    
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, code, in, inSz, out, outSz, &br, &g_ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        ok = GetOverlappedResult(h, &g_ov, &br, TRUE);
    }
    if (pRet) *pRet = br;
    return ok != FALSE;
}

int main() {
    printf("=== Stream Test v3 - OVERLAPPED IO ===\n\n");
    
    g_ovEvent = CreateEventW(NULL, TRUE, FALSE, NULL); // manual reset
    
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
    if (h == INVALID_HANDLE_VALUE) { printf("Open fail %lu\n", GetLastError()); return 1; }
    
    DWORD br;
    BYTE tmp[1040];
    io(h, 0x80882004, NULL, 0, tmp, 24);          // GET_DEVICE_INFO
    io(h, 0x80882820, NULL, 0, tmp, 4);            // GET_STATUS
    io(h, 0x808820C4, NULL, 0, tmp, 1040);         // GET_DEVICE_PROPS
    DWORD mode = 0; io(h, 0x80882804, &mode, 4);   // SET_MODE
    
    // Alloc control page + DMA buffers
    BYTE* mem = (BYTE*)VirtualAlloc(NULL, 40 * 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    BYTE* ctrl = mem;
    BYTE* dma = mem + 0x10000;
    printf("ctrl=%p dma=%p\n", ctrl, dma);
    
    // SET_CALLBACKS: shared mem addr + 3 offsets
    BYTE cb[32] = {};
    *(UINT64*)&cb[0] = (UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8] = 0x17C;
    *(UINT64*)&cb[16] = 0x180;
    *(UINT64*)&cb[24] = 0x184;
    bool ok = io(h, 0x80882880, cb, 32);
    printf("SET_CALLBACKS: %s\n", ok?"OK":"FAIL");
    
    // Mark sentinel values
    *(volatile DWORD*)(ctrl + 0x17C) = 0xDEADBEEF;
    *(volatile DWORD*)(ctrl + 0x180) = 0xDEADBEEF;
    *(volatile DWORD*)(ctrl + 0x184) = 0xDEADBEEF;
    
    BYTE config[292]; io(h, 0x80882808, NULL, 0, config, 292);
    printf("Rate: %u\n", *(DWORD*)config);
    
    BYTE chList[8200] = {};
    io(h, 0x8088280C, NULL, 0, chList, 8200, &br);
    DWORD numIn = *(DWORD*)&chList[0];
    DWORD numOut = *(DWORD*)&chList[4100];
    printf("Ch: %u in + %u out\n", numIn, numOut);
    
    DWORD bufSz = 128;
    io(h, 0x80882824, &bufSz, 4);
    printf("BufSize: 128\n");
    
    // SELECT + MAP all channels
    int chIdx = 0;
    auto setup = [&](DWORD blk, DWORD num) {
        for (DWORD i = 0; i < num; i++) {
            DWORD off = blk + 4 + i * 16;
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
    setup(0, numIn);
    setup(4100, numOut);
    printf("Mapped %d channels\n", chIdx);
    
    // START
    ok = io(h, 0x808828C8, NULL, 0);
    printf("START: %s (err=%lu)\n", ok?"OK":"FAIL", ok?0:GetLastError());
    
    if (ok) {
        printf("Polling for 3s...\n");
        DWORD prev17C = 0xDEADBEEF;
        int changes = 0;
        DWORD t0 = GetTickCount();
        while (GetTickCount() - t0 < 3000) {
            DWORD cur = *(volatile DWORD*)(ctrl + 0x17C);
            if (cur != prev17C) {
                changes++;
                if (changes <= 5)
                    printf("  [%4ums] 0x17C: 0x%08X->0x%08X  0x180=0x%08X 0x184=0x%08X\n",
                           GetTickCount()-t0, prev17C, cur,
                           *(volatile DWORD*)(ctrl+0x180), *(volatile DWORD*)(ctrl+0x184));
                prev17C = cur;
            }
            // Check first input DMA
            float* in0 = (float*)dma;
            if (changes == 0 && (GetTickCount()-t0) % 500 == 0) {
                bool hasData = false;
                for (int s = 0; s < 128; s++) if (in0[s] != 0.0f) { hasData = true; break; }
                if (hasData) printf("  Input buffer has data!\n");
            }
            Sleep(0);
        }
        printf("Changes in 3s: %d (%.1f Hz)\n", changes, changes/3.0);
        
        // Check ALL DMA buffers
        printf("\nDMA check:\n");
        for (int c = 0; c < chIdx; c++) {
            BYTE* buf = dma + c * 0x10000;
            bool hasData = false;
            for (int s = 0; s < 512; s++) if (buf[s] != 0) { hasData = true; break; }
            if (hasData) printf("  Ch[%d]: HAS DATA!\n", c);
        }
        
        // Scan control page
        printf("\nControl page non-zero:\n");
        for (int i = 0; i < 0x200; i += 4) {
            DWORD v = *(DWORD*)(ctrl + i);
            if (v != 0 && v != 0xDEADBEEF)
                printf("  +0x%03X: 0x%08X (%u)\n", i, v, v);
        }
        
        io(h, 0x808828CC, NULL, 0); // STOP
        printf("\nStopped\n");
    }
    
    VirtualFree(mem, 0, MEM_RELEASE);
    CloseHandle(h);
    return 0;
}
