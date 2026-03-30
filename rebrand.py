import os
import re

search_dirs = [r'd:\Autigravity\UMCasio\src', r'e:\Antigravity\成品开发\UMC\v7']
files_to_check = [
    r'd:\Autigravity\UMCasio\CMakeLists.txt',
    r'd:\Autigravity\UMCasio\sign_and_build.ps1',
    r'd:\Autigravity\UMCasio\version.txt'
]

for d in search_dirs:
    for root, _, files in os.walk(d):
        for file in files:
            if file.endswith(('.h', '.cpp', '.rc', '.iss', '.txt', '.ps1')):
                files_to_check.append(os.path.join(root, file))

def replace_in_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        return
    
    orig_content = content
    # UMC Ultra -> ASIO Ultra
    content = content.replace('UMC Ultra', 'ASIO Ultra')
    # UMCUltra -> ASIOUltra
    content = content.replace('UMCUltra', 'ASIOUltra')
    
    # Version bumps
    content = content.replace('6.2.1', '7.0.0')
    content = content.replace('6.2.2', '7.0.0')
    content = content.replace('6.2.4', '7.0.0')
    
    # Executable rename (Control Panel)
    content = content.replace('UMCControlPanel.exe', 'ASIOUltraControlPanel.exe')
    
    # Brand Ultra -> Brand ASIO Ultra 
    # Match: "Brand ASIO Ultra By ASMRTOP" instead of "Brand Ultra By ASMRTOP"
    # Note: earlier we replaced "UMC Ultra By ASMRTOP" -> "ASIO Ultra By ASMRTOP"
    # But for "Audient Ultra By ASMRTOP", we want "Audient ASIO Ultra By ASMRTOP"
    # First, let's fix AsioTargets.h specifically
    if filepath.endswith('AsioTargets.h'):
        content = re.sub(r'(?<!ASIO )([A-Za-z0-9\-]+) Ultra By ASMRTOP', r'\1 ASIO Ultra By ASMRTOP', content)
        content = content.replace('ASIO ASIO', 'ASIO') # Deduplicate if any
        
    if orig_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f'Updated: {filepath}')

for fp in files_to_check:
    replace_in_file(fp)

# Specifically fix the Output Name in CMakeLists.txt to match ASIOUltraControlPanel.exe
def fix_cmake():
    cf = r'd:\Autigravity\UMCasio\CMakeLists.txt'
    with open(cf, 'r', encoding='utf-8') as f:
        content = f.read()
    content = content.replace('add_executable(UMCControlPanel', 'add_executable(ASIOUltraControlPanel')
    content = content.replace('target_link_libraries(UMCControlPanel', 'target_link_libraries(ASIOUltraControlPanel')
    content = content.replace('install(TARGETS UMCControlPanel', 'install(TARGETS ASIOUltraControlPanel')
    content = content.replace('UMCControlPanel.cpp', 'UMCControlPanel.cpp') # kept same for cpp source
    with open(cf, 'w', encoding='utf-8') as f:
        f.write(content)
        
fix_cmake()

print("Rebranding to ASIO Ultra Complete!")
