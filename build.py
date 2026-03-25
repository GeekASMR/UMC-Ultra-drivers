import os
import subprocess

try:
    os.chdir(r"E:\Antigravity\成品开发\UMC\UMC_Ultra_Installer_Build")
    iscc = r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    subprocess.call([iscc, "setup.iss"])
except Exception as e:
    print(e)
