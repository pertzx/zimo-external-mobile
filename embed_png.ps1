$bytes = [System.IO.File]::ReadAllBytes("C:\Users\Márcio Ribeiro\Desktop\Pyerre\zimo-external-mobile\hesco_ov.png")
$out = "#pragma once`n`nconst int LogoWidth = 720;`nconst int LogoHeight = 1600;`n`nconst unsigned char LogoMenuRawRGBA[] = {`n"
for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($i % 16 -eq 0) { $out += "    " }
    $out += "0x" + $bytes[$i].ToString("X2") + ", "
    if ($i % 16 -eq 15) { $out += "`n" }
}
$out += "`n};`nconst size_t LogoMenuRawRGBASize = " + $bytes.Length + "`n"
[System.IO.File]::WriteAllText("C:\Users\Márcio Ribeiro\Desktop\Pyerre\zimo-external-mobile\app\src\main\cpp\Panel\Fonts\Bytes\BytesImg.hpp", $out)
"Done! Size: " + $bytes.Length