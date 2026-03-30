import os
from ftplib import FTP_TLS

server = "103.80.27.48"
user = "geek_asmrtop_cn"
password = "5yifcKJkbKefm4JR"

def download():
    print("Connecting to FTP...")
    ftp = FTP_TLS(server, timeout=15)
    ftp.login(user, password)
    ftp.prot_p() 
    ftp.set_pasv(True)
    print("Logged in!")
    
    with open(r'd:\Autigravity\UMCasio\index.html', 'wb') as f:
        ftp.retrbinary('RETR index.html', f.write)
                
    ftp.quit()
    print("FTP Download complete.")

if __name__ == "__main__":
    download()
