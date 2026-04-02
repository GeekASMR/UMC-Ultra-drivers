import sys

path = r'e:\Antigravity\成品开发\UMC\v7\setup.iss'
with open(path, 'rb') as f:
    content = f.read().decode('utf-8', 'ignore')

lines = content.split('\r\n')

# line 68 (0-indexed 67) is an empty line
# line 69 (0-indexed 68) is '; 一次性安装...'
# line 70 (0-indexed 69) is garbled
# line 74 (0-indexed 73) is garbled

lines.pop(73) # Remove garbled line 74
lines.pop(69) # Remove garbled line 70
lines[67] = '[Files]' # Re-insert [Files]

with open(path, 'wb') as f:
    f.write(b'\xef\xbb\xbf')
    if lines[0].startswith('\ufeff'):
         lines[0] = lines[0][1:]
    f.write('\r\n'.join(lines).encode('utf-8'))

print("Cleaned up missing [Files] section and garbled lines!")
