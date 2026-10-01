param(
    [string]$VoiceName = "Microsoft Huihui Desktop",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Speech

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot "..\firmware\main\audio\samples"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$format = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(
    32000,
    [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
    [System.Speech.AudioFormat.AudioChannel]::Mono
)

function Get-WavePcmData([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 44 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "RIFF") {
        throw "Not a RIFF wave file: $Path"
    }

    $offset = 12
    while ($offset + 8 -le $bytes.Length) {
        $chunkId = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $chunkSize = [BitConverter]::ToInt32($bytes, $offset + 4)
        $dataStart = $offset + 8
        if ($chunkSize -lt 0 -or $dataStart + $chunkSize -gt $bytes.Length) {
            throw "Invalid wave chunk in $Path"
        }
        if ($chunkId -eq "data") {
            $data = [byte[]]::new($chunkSize)
            [Array]::Copy($bytes, $dataStart, $data, 0, $chunkSize)
            return $data
        }
        $offset = $dataStart + $chunkSize + ($chunkSize % 2)
    }
    throw "Wave data chunk not found: $Path"
}

function Trim-And-NormalizePcm([byte[]]$Pcm) {
    if (($Pcm.Length % 2) -ne 0) {
        throw "PCM data must contain whole 16-bit samples"
    }
    $samples = [int16[]]::new($Pcm.Length / 2)
    [Buffer]::BlockCopy($Pcm, 0, $samples, 0, $Pcm.Length)

    $first = 0
    while ($first -lt $samples.Length -and [Math]::Abs([int]$samples[$first]) -lt 160) {
        $first++
    }
    $last = $samples.Length - 1
    while ($last -ge $first -and [Math]::Abs([int]$samples[$last]) -lt 160) {
        $last--
    }
    if ($last -lt $first) {
        throw "Synthesized prompt is silent"
    }

    $first = [Math]::Max(0, $first - 480)
    $last = [Math]::Min($samples.Length - 1, $last + 1600)
    $trimmed = [int16[]]::new($last - $first + 1)
    [Array]::Copy($samples, $first, $trimmed, 0, $trimmed.Length)

    $peak = 1
    foreach ($sample in $trimmed) {
        $peak = [Math]::Max($peak, [Math]::Abs([int]$sample))
    }
    $gain = [Math]::Min(1.0, 14000.0 / $peak)
    $fadeIn = [Math]::Min(160, $trimmed.Length)
    $fadeOut = [Math]::Min(640, $trimmed.Length)
    for ($i = 0; $i -lt $trimmed.Length; $i++) {
        $fade = 1.0
        if ($i -lt $fadeIn) {
            $fade = [Math]::Min($fade, $i / [double]$fadeIn)
        }
        $tail = $trimmed.Length - 1 - $i
        if ($tail -lt $fadeOut) {
            $fade = [Math]::Min($fade, $tail / [double]$fadeOut)
        }
        $trimmed[$i] = [int16][Math]::Round($trimmed[$i] * $gain * $fade)
    }

    $output = [byte[]]::new($trimmed.Length * 2)
    [Buffer]::BlockCopy($trimmed, 0, $output, 0, $output.Length)
    return $output
}

$prompts = [ordered]@{
    "tempo_slow.raw" = "慢速练习"
    "tempo_original.raw" = "原版"
    "tempo_fast.raw" = "快速节奏"
    "mode_beatbox.raw" = "鼓机模式"
    "mode_easyinput.raw" = "Easy Input 键盘模式"
}

$synth = [System.Speech.Synthesis.SpeechSynthesizer]::new()
try {
    $synth.SelectVoice($VoiceName)
    $synth.Rate = 0
    $synth.Volume = 100

    foreach ($entry in $prompts.GetEnumerator()) {
        $wavePath = Join-Path ([IO.Path]::GetTempPath()) ("easyinput-" + [Guid]::NewGuid() + ".wav")
        try {
            $synth.SetOutputToWaveFile($wavePath, $format)
            $synth.Speak([string]$entry.Value)
            $synth.SetOutputToNull()

            $pcm = Trim-And-NormalizePcm (Get-WavePcmData $wavePath)
            $rawPath = Join-Path $OutputDirectory ([string]$entry.Key)
            [IO.File]::WriteAllBytes($rawPath, $pcm)
            $durationMs = $pcm.Length * 1000.0 / 2 / 32000
            "{0}: {1:N0} ms, {2} bytes, voice={3}" -f $entry.Key, $durationMs, $pcm.Length, $VoiceName
        }
        finally {
            Remove-Item -LiteralPath $wavePath -Force -ErrorAction SilentlyContinue
        }
    }
}
finally {
    $synth.Dispose()
}
