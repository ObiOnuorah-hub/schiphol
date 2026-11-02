# Renders the radio phraseology fragments with the Windows voice and gives them a VHF radio sound.
# Output: Plugins/ATCSim/Data/Radio/<Pilot|Controller>/<token>.wav (22.05 kHz, 16 bit, mono). Import them into the
# project with the editor command ATC.ImportRadioVoice. Run again after changing the fragment list.
#
#   powershell -ExecutionPolicy Bypass -File Plugins\ATCSim\Tools\MakeRadioVoice.ps1

$ErrorActionPreference = 'Stop'
$Root = Join-Path $PSScriptRoot '..\Data\Radio'
Add-Type -AssemblyName System.Speech

Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.IO;

public static class RadioDsp
{
    const int Rate = 22050;

    static short[] ReadPcm(byte[] wav)
    {
        // Find the "data" chunk of a RIFF/WAVE file (16 bit mono)
        int pos = 12;
        while (pos + 8 <= wav.Length)
        {
            string id = System.Text.Encoding.ASCII.GetString(wav, pos, 4);
            int size = BitConverter.ToInt32(wav, pos + 4);
            if (id == "data")
            {
                short[] samples = new short[size / 2];
                Buffer.BlockCopy(wav, pos + 8, samples, 0, samples.Length * 2);
                return samples;
            }
            pos += 8 + size + (size & 1);
        }
        return new short[0];
    }

    public static void WriteWav(string path, float[] x)
    {
        using (var stream = new FileStream(path, FileMode.Create))
        using (var w = new BinaryWriter(stream))
        {
            w.Write(System.Text.Encoding.ASCII.GetBytes("RIFF")); w.Write(36 + x.Length * 2);
            w.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt ")); w.Write(16); w.Write((short)1); w.Write((short)1);
            w.Write(Rate); w.Write(Rate * 2); w.Write((short)2); w.Write((short)16);
            w.Write(System.Text.Encoding.ASCII.GetBytes("data")); w.Write(x.Length * 2);
            foreach (float v in x) w.Write((short)Math.Max(-32767, Math.Min(32767, v * 32767f)));
        }
    }

    // Biquad (RBJ cookbook), in place
    static void Biquad(float[] x, bool highPass, double freq, double q)
    {
        double w0 = 2 * Math.PI * freq / Rate, cos = Math.Cos(w0), alpha = Math.Sin(w0) / (2 * q);
        double b0, b1, b2;
        if (highPass) { b0 = (1 + cos) / 2; b1 = -(1 + cos); b2 = (1 + cos) / 2; }
        else { b0 = (1 - cos) / 2; b1 = 1 - cos; b2 = (1 - cos) / 2; }
        double a0 = 1 + alpha, a1 = -2 * cos, a2 = 1 - alpha;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < x.Length; i++)
        {
            double y = (b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = x[i]; y2 = y1; y1 = y; x[i] = (float)y;
        }
    }

    /// Speech (16 bit wav bytes) -> radio sound. pitch < 1 lowers the voice (and slows it a little).
    public static float[] Radio(byte[] wav, double pitch, double hissDb, int seed)
    {
        short[] pcm = ReadPcm(wav);
        // Trim the silence the synthesizer puts around every phrase
        int start = 0, end = pcm.Length - 1;
        while (start < end && Math.Abs((int)pcm[start]) < 300) start++;
        while (end > start && Math.Abs((int)pcm[end]) < 300) end--;
        start = Math.Max(0, start - 200); end = Math.Min(pcm.Length - 1, end + 600);
        int n = Math.Max(1, end - start + 1);

        // Resample: reading slower than the source lowers the pitch
        int outLength = (int)(n / pitch);
        float[] x = new float[outLength];
        for (int i = 0; i < outLength; i++)
        {
            double s = i * pitch; int k = (int)s; double f = s - k;
            float a = pcm[start + Math.Min(k, n - 1)] / 32768f, b = pcm[start + Math.Min(k + 1, n - 1)] / 32768f;
            x[i] = (float)(a + (b - a) * f);
        }

        // VHF voice band, a little overdriven, with hiss
        Biquad(x, true, 400, 0.707); Biquad(x, true, 400, 0.707);
        Biquad(x, false, 2800, 0.707); Biquad(x, false, 2800, 0.707);
        float peak = 1e-4f; foreach (float v in x) peak = Math.Max(peak, Math.Abs(v));
        var random = new Random(seed);
        double hiss = Math.Pow(10, hissDb / 20);
        for (int i = 0; i < x.Length; i++)
        {
            double v = Math.Tanh(2.5 * x[i] / peak) / Math.Tanh(2.5);
            x[i] = (float)(v * 0.7 + (random.NextDouble() * 2 - 1) * hiss);
        }
        Biquad(x, false, 3400, 0.707);
        return x;
    }

    /// The burst of noise when a radio keys up or drops (squelch).
    public static float[] Squelch(int milliseconds, double level, int seed)
    {
        int n = Rate * milliseconds / 1000;
        float[] x = new float[n];
        var random = new Random(seed);
        for (int i = 0; i < n; i++)
        {
            double envelope = Math.Min(1.0, i / (Rate * 0.004)) * Math.Exp(-3.0 * i / n);
            x[i] = (float)((random.NextDouble() * 2 - 1) * level * envelope);
        }
        Biquad(x, true, 500, 0.707); Biquad(x, false, 3000, 0.707);
        return x;
    }
}
'@

# Token -> spoken text. Digits in ICAO pronunciation (tree, fife, niner); letters in the NATO alphabet
$Fragments = [ordered]@{
    'd0' = 'zero'; 'd1' = 'one'; 'd2' = 'two'; 'd3' = 'tree'; 'd4' = 'four'; 'd5' = 'fife'; 'd6' = 'six'; 'd7' = 'seven'; 'd8' = 'eight'; 'd9' = 'niner'
    'la' = 'alfa'; 'lb' = 'bravo'; 'lc' = 'charlie'; 'ld' = 'delta'; 'le' = 'echo'; 'lf' = 'foxtrot'; 'lg' = 'golf'; 'lh' = 'hotel'; 'li' = 'india'
    'lj' = 'juliett'; 'lk' = 'kilo'; 'll' = 'lima'; 'lm' = 'mike'; 'ln' = 'november'; 'lo' = 'oscar'; 'lp' = 'papa'; 'lq' = 'quebec'; 'lr' = 'romeo'
    'ls' = 'sierra'; 'lt' = 'tango'; 'lu' = 'uniform'; 'lv' = 'victor'; 'lw' = 'whiskey'; 'lx' = 'x-ray'; 'ly' = 'yankee'; 'lz' = 'zulu'
    'klm' = 'K L M'; 'ezy' = 'Easy'; 'fin' = 'Finnair'; 'tra' = 'Transavia'; 'baw' = 'Speedbird'; 'dlh' = 'Lufthansa'; 'afr' = 'Air France'
    'left' = 'left'; 'right' = 'right'; 'center' = 'center'
    'north' = 'north'; 'east' = 'east'; 'south' = 'south'; 'west' = 'west'
    'schiphol_ground' = 'Schiphol Ground'; 'schiphol_tower' = 'Schiphol Tower'
    'stand' = 'stand'; 'runway' = 'runway'
    'request_pushback' = 'request push back'
    'pushback_approved_face' = 'push back approved, face'
    'ready_for_taxi' = 'ready for taxi'
    'taxi_to_holding_point' = 'taxi to holding point, runway'
    'holding_point' = 'holding point, runway'
    'ready_for_departure' = 'ready for departure'
    'wind' = 'wind'; 'degrees' = 'degrees'; 'knots' = 'knots'
    'cleared_for_takeoff' = 'cleared for takeoff'
    'holding_short' = 'holding short, runway'
    'cross_runway' = 'cross runway'
    'crossing_runway' = 'crossing runway'
    'vacated_runway' = 'vacated runway'
    'taxi_to_stand' = 'taxi to stand'
    'contact_departure' = 'contact departure, good day'
    'good_day' = 'good day'
    'hold_position' = 'hold position'
    'holding_position' = 'holding position'
    'roger' = 'roger'
}

$Voices = @{ 'Controller' = @{ Pitch = 1.0; Hiss = -40; Rate = 1 }; 'Pilot' = @{ Pitch = 0.86; Hiss = -32; Rate = 2 } }
$Synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$Format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$Seed = 1
foreach ($Voice in $Voices.Keys)
{
    $Folder = Join-Path $Root $Voice
    New-Item -ItemType Directory -Force $Folder | Out-Null
    $Synth.Rate = $Voices[$Voice].Rate
    foreach ($Token in $Fragments.Keys)
    {
        $Stream = New-Object System.IO.MemoryStream
        $Synth.SetOutputToAudioStream($Stream, $Format)
        $Synth.Speak($Fragments[$Token])
        $Synth.SetOutputToNull()
        # SetOutputToAudioStream writes raw PCM: wrap it in a minimal header for the DSP reader
        $Pcm = $Stream.ToArray()
        $Header = New-Object byte[] 44
        [System.Text.Encoding]::ASCII.GetBytes('RIFF').CopyTo($Header, 0); [BitConverter]::GetBytes(36 + $Pcm.Length).CopyTo($Header, 4)
        [System.Text.Encoding]::ASCII.GetBytes('WAVEfmt ').CopyTo($Header, 8); [BitConverter]::GetBytes(16).CopyTo($Header, 16)
        [BitConverter]::GetBytes([int16]1).CopyTo($Header, 20); [BitConverter]::GetBytes([int16]1).CopyTo($Header, 22)
        [BitConverter]::GetBytes(22050).CopyTo($Header, 24); [BitConverter]::GetBytes(44100).CopyTo($Header, 28)
        [BitConverter]::GetBytes([int16]2).CopyTo($Header, 32); [BitConverter]::GetBytes([int16]16).CopyTo($Header, 34)
        [System.Text.Encoding]::ASCII.GetBytes('data').CopyTo($Header, 36); [BitConverter]::GetBytes($Pcm.Length).CopyTo($Header, 40)
        $Samples = [RadioDsp]::Radio([byte[]]($Header + $Pcm), $Voices[$Voice].Pitch, $Voices[$Voice].Hiss, $Seed++)
        [RadioDsp]::WriteWav((Join-Path $Folder "$Token.wav"), $Samples)
    }
    [RadioDsp]::WriteWav((Join-Path $Folder 'squelch_open.wav'), [RadioDsp]::Squelch(70, 0.25, $Seed++))
    [RadioDsp]::WriteWav((Join-Path $Folder 'squelch_close.wav'), [RadioDsp]::Squelch(160, 0.35, $Seed++))
    Write-Output "$Voice : $($Fragments.Count + 2) fragments in $Folder"
}
