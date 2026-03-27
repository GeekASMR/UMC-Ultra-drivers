import sys
import struct
import hashlib

def examine_pe(filepath):
    print(f"File: {filepath}")
    try:
        with open(filepath, 'rb') as f:
            data = f.read()
            
        print(f"Size: {len(data)} bytes")
        
        # Calculate SHA256
        h = hashlib.sha256(data).hexdigest()
        print(f"SHA256: {h}")
        
        # PE offset
        pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
        print(f"PE offset: {hex(pe_offset)}")
        
        # Characteristics
        chars = struct.unpack_from('<H', data, pe_offset + 0x16)[0]
        print(f"Characteristics: {hex(chars)}")
        
        # IMAGE_FILE_DEBUG_STRIPPED is 0x0200
        debug_stripped = (chars & 0x0200) != 0
        print(f"DEBUG_STRIPPED (0x0200): {debug_stripped}")
        
        # Count int 3 (0xCC)
        cc_count = data.count(b'\xCC')
        print(f"int 3 (0xCC) count: {cc_count}")
        
        print("-" * 40)
    except Exception as e:
        print(f"Error reading file: {e}")

if __name__ == "__main__":
    examine_pe(r"e:\Antigravity\成品开发\UMC\Universal_Ultra_Installer_Build\VirtualAudioRouter.sys")
    examine_pe(r"E:\Personal\Desktop\签名\发布\gongkai - 副本\VirtualAudioRouter.sys")
