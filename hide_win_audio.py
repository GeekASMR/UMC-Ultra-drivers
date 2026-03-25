import os

target = r"D:\Program Files\PreSonus\Studio One 6\Plugins\windowsaudio.dll"
backup = r"D:\Program Files\PreSonus\Studio One 6\Plugins\windowsaudio.dll.bak"

if os.path.exists(target):
    try:
        os.rename(target, backup)
        print("Successfully hidden Windows Audio plugin.")
    except Exception as e:
        print(e)
else:
    print("Plugin not found or already hidden.")
