
import os
from ftplib import FTP_TLS

server = '103.80.27.48'
user = 'geek_asmrtop_cn'
password = '5yifcKJkbKefm4JR'

print('Connecting to FTP...')
ftp = FTP_TLS(server, timeout=15)
ftp.encoding = 'latin-1'
ftp.login(user, password)
ftp.prot_p()
ftp.set_pasv(True)

try:
    ftp.cwd('asio')
except:
    ftp.mkd('asio')
    ftp.cwd('asio')

local_web_dir = r'd:\Autigravity\UMCasio\server\web'

for filename in os.listdir(local_web_dir):
    filepath = os.path.join(local_web_dir, filename)
    if os.path.isfile(filepath):
        print(f'Uploading {filename} to asio/...')
        with open(filepath, 'rb') as f:
            ftp.storbinary(f'STOR {filename}', f)

ftp.quit()
print('FTP Backend Upload complete.')
