# Local HTTP test server (18080). /ka keeps the connection alive.
$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'http.log'
function Log($s) { Add-Content -Path $log -Value ((Get-Date).ToString('HH:mm:ss.fff') + ' ' + $s) }

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 18080)
$listener.Start()
Log 'HTTP listen 18080'
$connId = 0

# /img 测试图 (PNG, 由 System.Drawing 生成; 900ms 延迟用于观察 loading 动画)
Add-Type -AssemblyName System.Drawing
$pngPath = Join-Path $env:TEMP 'cui_img_test.png'
try {
    $bmp = New-Object System.Drawing.Bitmap 480, 300
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $c1 = [System.Drawing.Color]::FromArgb(255, 60, 120, 255)
    $c2 = [System.Drawing.Color]::FromArgb(255, 180, 80, 220)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point 0, 0),
        (New-Object System.Drawing.Point 480, 300), $c1, $c2)
    $g.FillRectangle($brush, 0, 0, 480, 300)
    $font = New-Object System.Drawing.Font 'Segoe UI', 26
    $g.DrawString('CUI Image', $font, [System.Drawing.Brushes]::White, 60, 120)
    $g.Dispose(); $bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Log ('generated ' + $pngPath)
} catch {
    Log ('img gen failed: ' + $_.Exception.Message)
}

while ($true) {
    $client = $listener.AcceptTcpClient()
    $connId++
    $reqNo = 0
    try {
        $st = $client.GetStream()
        $st.ReadTimeout = 8000
        while ($true) {
            $buf = New-Object byte[] 8192
            $ms = New-Object System.IO.MemoryStream
            $ok = $false
            while ($true) {
                $n = $st.Read($buf, 0, $buf.Length)
                if ($n -le 0) { break }
                $ms.Write($buf, 0, $n)
                $t = [Text.Encoding]::ASCII.GetString($ms.ToArray())
                if ($t.Contains("`r`n`r`n")) { $ok = $true; break }
            }
            if (-not $ok) { break }
            $reqNo++
            $t = [Text.Encoding]::ASCII.GetString($ms.ToArray())
            $parts = $t -split "`r`n`r`n", 2
            $head = $parts[0]
            $rest = if ($parts.Length -gt 1) { $parts[1] } else { '' }
            $lines = $head -split "`r`n"
            $first = $lines[0] -split ' '
            $method = $first[0]
            $path = $first[1]
            Log ("HTTP $method $path conn=$connId req=$reqNo")

            $clen = 0
            $wantClose = $false
            foreach ($ln in $lines) {
                if ($ln -match '^(?i)Content-Length:\s*(\d+)') { $clen = [int]$matches[1] }
                if ($ln -match '^(?i)Connection:\s*close') { $wantClose = $true }
            }
            $body = ''
            if ($clen -gt 0) {
                $all = New-Object System.IO.MemoryStream
                if ($rest) {
                    $rb = [Text.Encoding]::UTF8.GetBytes($rest)
                    $all.Write($rb, 0, $rb.Length)
                }
                while ($all.Length -lt $clen) {
                    $n = $st.Read($buf, 0, $buf.Length)
                    if ($n -le 0) { break }
                    $all.Write($buf, 0, $n)
                }
                $body = [Text.Encoding]::UTF8.GetString($all.ToArray())
            }

            if ($path -eq '/img') {
                Start-Sleep -Milliseconds 900
                $b = [System.IO.File]::ReadAllBytes($pngPath)
                $hdr = "HTTP/1.1 200 OK`r`nContent-Type: image/png`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length); $st.Write($b, 0, $b.Length)
                $wantClose = $true
            }
            elseif ($path -eq '/chunked') {
                $hdr = "HTTP/1.1 200 OK`r`nContent-Type: text/plain`r`nTransfer-Encoding: chunked`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length)
                foreach ($c in @('chunk-a;', 'chunk-b;', 'chunk-c')) {
                    $cb = [Text.Encoding]::UTF8.GetBytes($c)
                    $ch = [Text.Encoding]::ASCII.GetBytes(('{0:x}' -f $cb.Length) + "`r`n")
                    $st.Write($ch, 0, $ch.Length)
                    $st.Write($cb, 0, $cb.Length)
                    $crlf = [Text.Encoding]::ASCII.GetBytes("`r`n")
                    $st.Write($crlf, 0, $crlf.Length)
                    $st.Flush()
                    Start-Sleep -Milliseconds 60
                }
                $end = [Text.Encoding]::ASCII.GetBytes("0`r`n`r`n")
                $st.Write($end, 0, $end.Length)
                $wantClose = $true
            }
            elseif ($path -eq '/ka') {
                $b = [Text.Encoding]::UTF8.GetBytes('keep-alive body')
                $hdr = "HTTP/1.1 200 OK`r`nContent-Type: text/plain`r`nContent-Length: $($b.Length)`r`nConnection: keep-alive`r`nX-Conn-Req: $reqNo`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length); $st.Write($b, 0, $b.Length)
            }
            elseif ($path -eq '/slow') {
                Start-Sleep -Milliseconds 2500
                $b = [Text.Encoding]::UTF8.GetBytes('slow done')
                $hdr = "HTTP/1.1 200 OK`r`nContent-Type: text/plain`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length); $st.Write($b, 0, $b.Length)
                $wantClose = $true
            }
            elseif ($path -eq '/redirect') {
                $hdr = "HTTP/1.1 302 Found`r`nLocation: /hello`r`nContent-Length: 0`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length)
                $wantClose = $true
            }
            elseif ($path -eq '/json') {
                $b = [Text.Encoding]::UTF8.GetBytes('{"ok":true,"n":42}')
                $hdr = "HTTP/1.1 200 OK`r`nContent-Type: application/json`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length); $st.Write($b, 0, $b.Length)
                $wantClose = $true
            }
            elseif ($method -eq 'POST') {
                $b = [Text.Encoding]::UTF8.GetBytes(('{"got":' + ($body | ConvertTo-Json -Compress) + ',"len":' + $body.Length + '}'))
                $hdr = "HTTP/1.1 200 OK`r`nContent-Type: application/json`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length); $st.Write($b, 0, $b.Length)
                $wantClose = $true
            }
            elseif ($path -eq '/hello') {
                $b = [Text.Encoding]::UTF8.GetBytes('hello from ps server')
                $hdr = "HTTP/1.1 200 OK`r`nContent-Type: text/plain; charset=utf-8`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length); $st.Write($b, 0, $b.Length)
                $wantClose = $true
            }
            else {
                $b = [Text.Encoding]::UTF8.GetBytes('not found')
                $hdr = "HTTP/1.1 404 Not Found`r`nContent-Type: text/plain`r`nContent-Length: $($b.Length)`r`nConnection: close`r`n`r`n"
                $hb = [Text.Encoding]::ASCII.GetBytes($hdr)
                $st.Write($hb, 0, $hb.Length); $st.Write($b, 0, $b.Length)
                $wantClose = $true
            }
            $st.Flush()
            if ($wantClose) { break }
        }
    } catch {
        Log ("HTTP conn=$connId error: " + $_.Exception.Message)
    } finally {
        $client.Close()
    }
}
