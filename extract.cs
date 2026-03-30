using System;
using System.Drawing;
using System.IO;

class Program {
    static void Main() {
        try {
            Icon icon = Icon.ExtractAssociatedIcon(@"C:\Program Files\Behringer\UMC_Audio_Driver\UMCAudioCplApp.exe");
            if (icon == null) throw new Exception("Failed to extract.");
            Bitmap bmp = icon.ToBitmap();
            for (int y = 0; y < bmp.Height; y++) {
                for (int x = 0; x < bmp.Width; x++) {
                    Color p = bmp.GetPixel(x, y);
                    if (p.A == 0) continue;
                    int brightness = (p.R + p.G + p.B) / 3;
                    if (brightness > 180) {
                        bmp.SetPixel(x, y, Color.FromArgb(p.A, 240, 240, 240));
                    } else if (brightness > 10) {
                        bmp.SetPixel(x, y, Color.FromArgb(p.A, 20, 20, 20));
                    }
                }
            }
            Icon newIcon = Icon.FromHandle(bmp.GetHicon());
            using (FileStream fs = new FileStream(@"d:\Autigravity\UMCasio\black_umc.ico", FileMode.Create)) {
                newIcon.Save(fs);
            }
            Console.WriteLine("Success");
        } catch (Exception ex) {
            Console.WriteLine("Error: " + ex.Message);
            
            try {
                Icon icon = Icon.ExtractAssociatedIcon(@"C:\Program Files\BEHRINGER\UMC_Audio_Driver\x64\UMCAudioCplApp.exe");
                if (icon == null) return;
                Bitmap bmp = icon.ToBitmap();
                for (int y = 0; y < bmp.Height; y++) {
                    for (int x = 0; x < bmp.Width; x++) {
                        Color p = bmp.GetPixel(x, y);
                        if (p.A == 0) continue;
                        int brightness = (p.R + p.G + p.B) / 3;
                        if (brightness > 180) {
                            bmp.SetPixel(x, y, Color.FromArgb(p.A, 240, 240, 240));
                        } else if (brightness > 10) {
                            bmp.SetPixel(x, y, Color.FromArgb(p.A, 20, 20, 20));
                        }
                    }
                }
                Icon newIcon = Icon.FromHandle(bmp.GetHicon());
                using (FileStream fs = new FileStream(@"d:\Autigravity\UMCasio\black_umc.ico", FileMode.Create)) {
                    newIcon.Save(fs);
                }
                Console.WriteLine("Success");
            } catch (Exception e2) {
                Console.WriteLine("Error2: " + e2.Message);
            }
        }
    }
}
