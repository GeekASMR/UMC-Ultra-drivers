#include <windows.h>
#include <wincrypt.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

bool verifyRSASig(const std::string& data, const std::string& sigHex) {
    if (sigHex.length() != 512) return false;

    const char* pubKeyPEM = 
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAnVMLCW268pNs8qFsZ86J\n"
        "SZIsISafx8q46BIGbNHtqljOgJlwCrc+yRr5LIu/pmTUQNkj66OdU1bCTbaa0BlS\n"
        "oWORK/M8X2wlWyEiEKy9YcJdSbinDsTfKNz2pcZxpa6jtI2bwb8sDYHmxun3I9Xa\n"
        "MNVW3fs99eRgyNq9Wi35A7Y91uSwpn9LdWH1sgsZnppCV3m944YUTSlwNiWAGd0z\n"
        "eyJ35sBd1CK4gU2/JHwCmVlNQ/lIXSqRbk9YTGSQcdP3IgJtuIPwVO8iW7oeZUOW\n"
        "TTby8XHcIDvxpqJcVbzUXeOE+3TGoWeG/uMxM2bxYo+PuGtQCS2Ez60w/AUloxUv\n"
        "bwIDAQAB\n"
        "-----END PUBLIC KEY-----\n";

    DWORD derLen = 0;
    if (!CryptStringToBinaryA(pubKeyPEM, 0, CRYPT_STRING_BASE64HEADER, nullptr, &derLen, nullptr, nullptr)) { std::cout << "CryptStringToBinaryA len fail\n"; return false; }
    BYTE* der = new BYTE[derLen];
    if (!CryptStringToBinaryA(pubKeyPEM, 0, CRYPT_STRING_BASE64HEADER, der, &derLen, nullptr, nullptr)) { std::cout << "CryptStringToBinaryA fail\n"; delete[] der; return false; }

    PCERT_PUBLIC_KEY_INFO pki = nullptr;
    DWORD pkiLen = 0;
    bool bRet = false;
    if (CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, der, derLen, CRYPT_DECODE_ALLOC_FLAG, nullptr, &pki, &pkiLen)) {
        HCRYPTPROV hProv = 0;
        if (CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            HCRYPTKEY hKey = 0;
            if (CryptImportPublicKeyInfo(hProv, X509_ASN_ENCODING, pki, &hKey)) {
                HCRYPTHASH hHash = 0;
                if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                    CryptHashData(hHash, (const BYTE*)data.c_str(), (DWORD)data.length(), 0);
                    
                    BYTE sigBin[256];
                    for (int i=0; i<256; i++) {
                        char sub[3] = { sigHex[i*2], sigHex[i*2+1], 0 };
                        sigBin[255 - i] = (BYTE)strtol(sub, nullptr, 16); // 反转字节序匹配 CryptoAPI Little-Endian
                    }

                    if (CryptVerifySignatureA(hHash, sigBin, 256, hKey, nullptr, 0)) {
                        bRet = true;
                    } else {
                        std::cout << "CryptVerifySignatureA fail: " << GetLastError() << "\n";
                    }
                    CryptDestroyHash(hHash);
                } else { std::cout << "CryptCreateHash fail\n"; }
                CryptDestroyKey(hKey);
            } else { std::cout << "CryptImportPublicKeyInfo fail\n"; }
            CryptReleaseContext(hProv, 0);
        } else { std::cout << "CryptAcquireContextA fail\n"; }
        LocalFree(pki);
    } else { std::cout << "CryptDecodeObjectEx fail\n"; }
    delete[] der;
    return bRet;
}

int main() {
    std::string sig = "88f5ec545b0e1f7f9794f9e81940130471aed03fe33031eb1d47b1e14d6f9b1281cfec6cb560dbf51f6de9b02ed49456d05440edcda4b571b875d6a738d9cc763815e71ccd58046492b3632ffabd6656bf4ca198e56d576323b0e8e880ffd92ff8b4998a6d86d800b2b8d824038e07f07128cc43326b3803778636965281f785fb8163151d2f748f942f7bde97650f580aa998b6e519f10c21043688f534e945eff578e9b3f49b8b0ff8b4eec95cdd9845a696019079c5131cf103083cc724dd2b45a99f3c467c44ccb5b3df5af12e1333a787794162b6efdff68103c4feffdc804898ac4abebcee59987d29bf5e61db7188530bbd0bf5946bda054d6cb6277e";
    std::string data = "invalid|ef946ac42653cf52";
    if (verifyRSASig(data, sig)) {
        std::cout << "SUCCESS\n";
    } else {
        std::cout << "FAILED\n";
    }
    return 0;
}
