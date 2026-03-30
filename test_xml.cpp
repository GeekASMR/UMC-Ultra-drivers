#include <iostream>
#include <string>
#include <vector>
#include <regex>

int main() {
    std::string attStr = "<Attributes sampleRate=\"48000\" masterDevice=\"{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}\" deviceBlockSize=\"256\" dropOutProtectionLevel=\"0\" floatType=\"0\" suspendInBackground=\"0\" silencePolicy=\"1\" failedDevices=\"{A1B2C3D4-E5F6-7890-ABCD-EF1234560000},,\"/>";
    std::regex failedRgx("failedDevices=\"([^\"]+)\"");
    std::smatch matchFail;
    std::vector<std::string> existingFailed;
    if (std::regex_search(attStr, matchFail, failedRgx)) {
        std::string found = matchFail[1].str();
        std::cout << "Found: " << found << "\n";
        size_t pos = 0;
        while ((pos = found.find("{")) != std::string::npos) {
            size_t end = found.find("}", pos);
            if (end != std::string::npos) {
                existingFailed.push_back(found.substr(pos, end - pos + 1));
                found.erase(0, end + 1);
            } else break;
        }
    }
    for(size_t i=0;i<existingFailed.size();i++) std::cout << "Parsed: " << existingFailed[i] << "\n";
    return 0;
}