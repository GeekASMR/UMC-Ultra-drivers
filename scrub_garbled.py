import sys

path = r'e:\Antigravity\成品开发\UMC\v7\setup.iss'
with open(path, 'rb') as f:
    content = f.read().decode('utf-8', 'ignore')

lines = content.split('\r\n')
new_lines = []

for line in lines:
    if line.strip().startswith('//') and ('' in line or '?' in line or 'í' in line or 'ï' in line or '»' in line):
        continue  # Delete garbled comments
    if line.strip().startswith(';') and ('' in line or '?' in line or 'í' in line or 'ï' in line or '»' in line):
        continue  # Delete garbled comments
    new_lines.append(line)

with open(path, 'wb') as f:
    f.write(b'\xef\xbb\xbf')
    if new_lines[0].startswith('\ufeff'):
         new_lines[0] = new_lines[0][1:]
    f.write('\r\n'.join(new_lines).encode('utf-8'))

print("Scrubbed all remaining garbled comments!")
