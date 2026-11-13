# 本机 WebSocket 测试服务器 (18081, 回显)
$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'ws.log'
function Log($s) { Add-Content -Path $log -Value ((Get-Date).ToString('HH:mm:ss.fff') + ' ' + $s) }

function Read-Exact($stream, $n) {
    $b = New-Object byte[] $n
    $o = 0
    while ($o -lt $n) {
        $r = $stream.Read($b, $o, $n - $o)
        if ($r -le 0) { throw 'eof' }
        $o += $r
    }
    return ,$b
}

function Send-Frame($stream, $op, [byte[]]$payload) {
    $len = $payload.Length
    if ($len -lt 126) {
        $fh = New-Object byte[] 2
        $fh[0] = [byte](0x80 -bor $op)
        $fh[1] = [byte]$len
    } elseif ($len -lt 65536) {
        $fh = New-Object byte[] 4
        $fh[0] = 0x80 -bor $op; $fh[1] = 126
        $fh[2] = [byte](($len -shr 8) -band 0xFF); $fh[3] = [byte]($len -band 0xFF)
    } else {
        $fh = New-Object byte[] 10
        $fh[0] = 0x80 -bor $op; $fh[1] = 127
        $v = [uint64]$len
        for ($i = 0; $i -lt 8; $i++) { $fh[2 + $i] = [byte](($v -shr (56 - $i * 8)) -band 0xFF) }
    }
    $stream.Write($fh, 0, $fh.Length)
    if ($len -gt 0) { $stream.Write($payload, 0, $len) }
    $stream.Flush()
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 18081)
$listener.Start()
Log 'WS listen 18081'

while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
        $st = $client.GetStream()
        $st.ReadTimeout = 60000
        $buf = New-Object byte[] 4096
        $ms = New-Object System.IO.MemoryStream
        while ($true) {
            $n = $st.Read($buf, 0, $buf.Length)
            if ($n -le 0) { throw 'eof' }
            $ms.Write($buf, 0, $n)
            $t = [Text.Encoding]::ASCII.GetString($ms.ToArray())
            if ($t.Contains("`r`n`r`n")) { break }
        }
        $t = [Text.Encoding]::ASCII.GetString($ms.ToArray())
        $key = ''
        foreach ($ln in ($t -split "`r`n")) {
            if ($ln -match '^(?i)Sec-WebSocket-Key:\s*(.+)$') { $key = $matches[1].Trim() }
        }
        $sha = [System.Security.Cryptography.SHA1]::Create()
        $raw = [Text.Encoding]::ASCII.GetBytes($key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
        $accept = [Convert]::ToBase64String($sha.ComputeHash($raw))
        $resp = "HTTP/1.1 101 Switching Protocols`r`nUpgrade: websocket`r`nConnection: Upgrade`r`nSec-WebSocket-Accept: $accept`r`n`r`n"
        $rb = [Text.Encoding]::ASCII.GetBytes($resp)
        $st.Write($rb, 0, $rb.Length); $st.Flush()
        Log 'WS upgraded'

        while ($true) {
            $h = Read-Exact $st 2
            $op = $h[0] -band 0x0F
            $len = $h[1] -band 0x7F
            $masked = ($h[1] -band 0x80) -ne 0
            if ($len -eq 126) {
                $e = Read-Exact $st 2
                $len = ($e[0] -shl 8) -bor $e[1]
            } elseif ($len -eq 127) {
                $e = Read-Exact $st 8
                $len = 0
                foreach ($x in $e) { $len = $len * 256 + $x }
            }
            $mask = $null
            if ($masked) { $mask = Read-Exact $st 4 }
            $p = @()
            if ($len -gt 0) { $p = Read-Exact $st $len } else { $p = New-Object byte[] 0 }
            if ($masked) {
                for ($i = 0; $i -lt $len; $i++) { $p[$i] = $p[$i] -bxor $mask[$i % 4] }
            }
            if ($op -eq 8) { Log 'WS close frame'; Send-Frame $st 8 $p; break }
            if ($op -eq 9) { Send-Frame $st 10 $p; continue }
            if ($op -eq 10) { continue }
            if ($op -eq 1 -or $op -eq 2) {
                $text = [Text.Encoding]::UTF8.GetString($p)
                Log ("WS recv op=$op len=$($p.Length) " + $text)
                $echo = [Text.Encoding]::UTF8.GetBytes('echo:' + $text)
                Send-Frame $st 1 $echo
            }
        }
    } catch {
        Log ("WS error: " + $_.Exception.Message)
    } finally {
        $client.Close()
    }
}
