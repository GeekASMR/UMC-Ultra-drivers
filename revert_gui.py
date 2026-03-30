import os

def revert_gui_names(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        return
    
    orig_content = content
    # Note: the Control Panel and License Manager expect standard registry keys
    content = content.replace('ASIO Ultra By ASMRTOP', 'UMC Ultra By ASMRTOP')
    content = content.replace('ASIO Ultra By ASMRTOP(trial)', 'UMC Ultra By ASMRTOP(trial)')
    content = content.replace('ASIO Ultra By ASMRTOP(Expired)', 'UMC Ultra By ASMRTOP(Expired)')
    
    if orig_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f'Reverted GUI names in: {filepath}')

search_dir = r'd:\Autigravity\UMCasio\src'
for root, _, files in os.walk(search_dir):
    for file in files:
        if file.endswith(('.h', '.cpp', '.rc')):
            revert_gui_names(os.path.join(root, file))
