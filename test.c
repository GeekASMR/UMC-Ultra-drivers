#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#pragma comment(lib, "crypt32.lib")

int main() {
    const char* pubKeyPEM = 
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAnVMLCW268pNs8qFsZ86J\n"
        "-----END PUBLIC KEY-----\n";
    DWORD len = 0;
    if(CryptStringToBinaryA(pubKeyPEM, 0, CRYPT_STRING_BASE64HEADER, NULL, &len, NULL, NULL)) {
        printf("OK %d\n", len);
    } else {
        printf("FAIL %d\n", GetLastError());
    }
    return 0;
}
