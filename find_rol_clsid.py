import winreg

def list_asio():
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\ASIO")
    except Exception as e:
        print("Could not open ASIO key", e)
        return

    i = 0
    while True:
        try:
            subkey_name = winreg.EnumKey(key, i)
            subkey = winreg.OpenKey(key, subkey_name)
            clsid, _ = winreg.QueryValueEx(subkey, "CLSID")
            desc, _ = winreg.QueryValueEx(subkey, "Description")
            print(f"[{subkey_name}] -> CLSID: {clsid} | Desc: {desc}")
            i += 1
        except Exception as e:
            break

list_asio()
