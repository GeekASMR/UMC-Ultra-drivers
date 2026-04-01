$path = 'd:\Autigravity\UMCasio\ASIO_Test_Audio.wav'
$sampleRate = 48000
$duration = 5
$numSamples = $sampleRate * $duration
$dataSize = $numSamples * 2
$fileSize = 36 + $dataSize

$fs = [System.IO.File]::Create($path)
$bw = [System.IO.BinaryWriter]::new($fs)

# RIFF header
$bw.Write([char[]]"RIFF")
$bw.Write([int]($fileSize))
$bw.Write([char[]]"WAVE")

# fmt chunk
$bw.Write([char[]]"fmt ")
$bw.Write([int]16)
$bw.Write([short]1)
$bw.Write([short]1)
$bw.Write([int]$sampleRate)
$bw.Write([int]($sampleRate * 2))
$bw.Write([short]2)
$bw.Write([short]16)

# data chunk
$bw.Write([char[]]"data")
$bw.Write([int]$dataSize)

# generate sine wave
$freq = 440.0
for ($i = 0; $i -lt $numSamples; $i++) {
    $t = $i / $sampleRate
    $sample = [System.Math]::Sin(2 * [System.Math]::PI * $freq * $t)
    $shortVal = [short]($sample * 32767.0)
    $bw.Write($shortVal)
}

$bw.Close()
$fs.Close()
Write-Host "Generated $($path) successfully."
