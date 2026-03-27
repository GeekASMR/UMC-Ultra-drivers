import os
from ftplib import FTP_TLS

server = "103.80.27.48"
user = "geek_asmrtop_cn"
password = "5yifcKJkbKefm4JR"
local_dir = r"d:\Autigravity\UMCasio\server\web"
ftp_dir = "asio"

def upload():
    print("Connecting to FTP...")
    ftp = FTP_TLS(server, timeout=15)
    
    # login with TLS
    ftp.login(user, password)
    ftp.prot_p() 
    ftp.set_pasv(True)
    print("Logged in!")
    
    # Check if 'asio' exists, if not create it
    if ftp_dir not in ftp.nlst():
        print(f"Creating directory {ftp_dir}")
        ftp.mkd(ftp_dir)
        
    ftp.cwd(ftp_dir)
    
    for filename in os.listdir(local_dir):
        filepath = os.path.join(local_dir, filename)
        if os.path.isfile(filepath):
            print(f"Uploading {filename}...")
            with open(filepath, 'rb') as f:
                ftp.storbinary(f'STOR {filename}', f)
                
    ftp.quit()
    print("FTP Upload complete.")

if __name__ == "__main__":
    upload()
