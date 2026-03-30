import os
import re

def revert_brand_names(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        return
    
    orig_content = content
    # For UMC explicitly
    content = content.replace('ASIO Ultra By ASMRTOP', 'UMC Ultra By ASMRTOP')
    # For generated brands: "Audient ASIO Ultra By ASMRTOP" -> "Audient Ultra By ASMRTOP"
    content = re.sub(r'([A-Za-z0-9\-]+) ASIO Ultra By ASMRTOP', r'\1 Ultra By ASMRTOP', content)
    
    if orig_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f'Reverted DAW targets: {filepath}')

revert_brand_names(r'd:\Autigravity\UMCasio\src\AsioTargets.h')
revert_brand_names(r'd:\Autigravity\UMCasio\src\driver\BehringerASIO.cpp')
revert_brand_names(r'd:\Autigravity\UMCasio\src\com\dllmain.cpp')
