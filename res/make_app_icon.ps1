# make_app_icon.ps1 - build res\cui_app.ico (16 + 48, PNG encoded)
#
# Design: blue->violet rounded plate + white "CUI" monogram. Letters are
# drawn as strokes (not scaled text) so the 16 px version stays legible.
# PNG + Up filter keeps the two-image ICO near 2 KB.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File res\make_app_icon.ps1
# NOTE: keep this file ASCII-only (PowerShell 5.1 reads BOM-less UTF-8 as ANSI).
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'
$outDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sizes = @(16, 48)

function New-RoundedPath([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [single]($r * 2)
    if ($d -le 0) { $d = [single]0.01 }
    $p.AddArc($x, $y, $d, $d, [single]180, [single]90)
    $p.AddArc([single]($x + $w - $d), $y, $d, $d, [single]270, [single]90)
    $p.AddArc([single]($x + $w - $d), [single]($y + $h - $d), $d, $d, [single]0, [single]90)
    $p.AddArc($x, [single]($y + $h - $d), $d, $d, [single]90, [single]90)
    $p.CloseFigure()
    return $p
}

function P([single]$v, [single]$s) { return [single]($v * $s) }

# ---- minimal PNG encoder ------------------------------------------------

function BE32([uint32]$v) {
    return [byte[]]@(
        [byte](($v -shr 24) -band 0xFF),
        [byte](($v -shr 16) -band 0xFF),
        [byte](($v -shr 8) -band 0xFF),
        [byte]($v -band 0xFF))
}

function Adler32([byte[]]$d) {
    [uint32]$a = 1
    [uint32]$b = 0
    foreach ($x in $d) {
        $a = ($a + [uint32]$x) % 65521
        $b = ($b + $a) % 65521
    }
    return [uint32](($b -shl 16) -bor $a)
}

$script:crcTable = $null
function Get-Crc32([byte[]]$d) {
    if ($null -eq $script:crcTable) {
        $t = New-Object 'uint32[]' 256
        for ($n = 0; $n -lt 256; $n++) {
            [uint32]$c = [uint32]$n
            for ($k = 0; $k -lt 8; $k++) {
                if ($c -band 1) { $c = [uint32](3988292384 -bxor ($c -shr 1)) }
                else { $c = [uint32]($c -shr 1) }
            }
            $t[$n] = $c
        }
        $script:crcTable = $t
    }
    [uint32]$crc = [uint32]::MaxValue
    foreach ($x in $d) {
        $crc = [uint32]($script:crcTable[($crc -bxor [uint32]$x) -band 0xFF] -bxor ($crc -shr 8))
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function Write-Chunk([System.IO.MemoryStream]$s, [string]$type, [byte[]]$data) {
    $len = BE32 ([uint32]($data.Length))
    $s.Write($len, 0, 4)
    $t = [System.Text.Encoding]::ASCII.GetBytes($type)
    $s.Write($t, 0, 4)
    if ($data.Length -gt 0) { $s.Write($data, 0, $data.Length) }
    $chk = New-Object byte[] (4 + $data.Length)
    [Array]::Copy($t, 0, $chk, 0, 4)
    if ($data.Length -gt 0) { [Array]::Copy($data, 0, $chk, 4, $data.Length) }
    $crc = BE32 (Get-Crc32 $chk)
    $s.Write($crc, 0, 4)
}

function Encode-PngBytes([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width
    $h = $bmp.Height
    $rect = [System.Drawing.Rectangle]::new(0, 0, $w, $h)
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $bd.Stride
    $raw = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $raw, 0, $raw.Length)
    $bmp.UnlockBits($bd)

    $rb = $w * 4
    $rgba = New-Object byte[] ($rb * $h)
    for ($y = 0; $y -lt $h; $y++) {
        $src = $y * $stride
        $dst = $y * $rb
        for ($i = 0; $i -lt $rb; $i += 4) {
            # 6 bits per RGB channel: hides quantization on the gradient,
            # makes rows collapse under the Up filter (much smaller PNG).
            $rgba[$dst + $i] = $raw[$src + $i + 2] -band 0xFC
            $rgba[$dst + $i + 1] = $raw[$src + $i + 1] -band 0xFC
            $rgba[$dst + $i + 2] = $raw[$src + $i] -band 0xFC
            $rgba[$dst + $i + 3] = $raw[$src + $i + 3]
        }
    }

    $flen = 1 + $rb
    $filtered = New-Object byte[] ($flen * $h)
    for ($y = 0; $y -lt $h; $y++) {
        $dst = $y * $flen
        if ($y -eq 0) {
            $filtered[$dst] = [byte]0
            for ($i = 0; $i -lt $rb; $i++) { $filtered[$dst + 1 + $i] = $rgba[$i] }
        } else {
            $filtered[$dst] = [byte]2
            for ($i = 0; $i -lt $rb; $i++) {
                $cur = $rgba[$y * $rb + $i]
                $up = $rgba[($y - 1) * $rb + $i]
                $filtered[$dst + 1 + $i] = [byte](($cur - $up) -band 0xFF)
            }
        }
    }

    $comp = New-Object System.IO.MemoryStream
    $ds = New-Object System.IO.Compression.DeflateStream($comp,
        [System.IO.Compression.CompressionMode]::Compress, $true)
    $ds.Write($filtered, 0, $filtered.Length)
    $ds.Close()
    $deflated = $comp.ToArray()

    $zs = New-Object System.IO.MemoryStream
    $zs.WriteByte(0x78)
    $zs.WriteByte(0x9C)
    $zs.Write($deflated, 0, $deflated.Length)
    $ad = BE32 (Adler32 $filtered)
    $zs.Write($ad, 0, 4)

    $png = New-Object System.IO.MemoryStream
    $sig = [byte[]]@(137, 80, 78, 71, 13, 10, 26, 10)
    $png.Write($sig, 0, 8)
    $ihdr = New-Object byte[] 13
    [Array]::Copy((BE32 ([uint32]$w)), 0, $ihdr, 0, 4)
    [Array]::Copy((BE32 ([uint32]$h)), 0, $ihdr, 4, 4)
    $ihdr[8] = 8
    $ihdr[9] = 6
    $ihdr[10] = 0
    $ihdr[11] = 0
    $ihdr[12] = 0
    Write-Chunk $png 'IHDR' $ihdr
    Write-Chunk $png 'IDAT' $zs.ToArray()
    Write-Chunk $png 'IEND' (New-Object byte[] 0)
    return $png.ToArray()
}

function New-IconBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = [single]($size / 256.0)
    $bx = [single]($s * 8)
    $bs = [single]($s * 240)
    $rad = [single]($s * 54)
    $rect = [System.Drawing.RectangleF]::new($bx, $bx, $bs, $bs)
    $path = New-RoundedPath $bx $bx $bs $bs $rad
    $c1 = [System.Drawing.Color]::FromArgb(255, 72, 132, 255)
    $c2 = [System.Drawing.Color]::FromArgb(255, 126, 72, 255)
    $lg = New-Object System.Drawing.Drawing2D.LinearGradientBrush -ArgumentList @($rect, $c1, $c2, [single]55)
    $g.FillPath($lg, $path)

    # soft top highlight
    $ha = [System.Drawing.Color]::FromArgb(52, 255, 255, 255)
    $hb = [System.Drawing.Color]::FromArgb(0, 255, 255, 255)
    $hl = New-Object System.Drawing.Drawing2D.LinearGradientBrush -ArgumentList @($rect, $ha, $hb, [single]90)
    $g.FillPath($hl, $path)

    # "CUI" monogram, drawn as strokes so 16 px stays legible.
    $lw = [single](P 26 $s)
    if ($lw -lt 1.6) { $lw = [single]1.6 }
    $white = [System.Drawing.Color]::White
    $cPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $cPath.AddArc((P 28 $s), (P 58 $s), (P 72 $s), (P 140 $s),
        [single]45, [single]270)
    $uPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $uPath.StartFigure()
    $uPath.AddLine((P 140 $s), (P 60 $s), (P 140 $s), (P 140 $s))
    $uPath.AddBezier(
        [System.Drawing.PointF]::new((P 140 $s), (P 140 $s)),
        [System.Drawing.PointF]::new((P 140 $s), (P 182 $s)),
        [System.Drawing.PointF]::new((P 182 $s), (P 182 $s)),
        [System.Drawing.PointF]::new((P 182 $s), (P 140 $s)))
    $uPath.AddLine((P 182 $s), (P 140 $s), (P 182 $s), (P 60 $s))

    $pen = New-Object System.Drawing.Pen -ArgumentList @($white, $lw)
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

    if ($size -ge 32) {
        # subtle depth; skipped at 16 px where it only blurs the strokes
        $spen = New-Object System.Drawing.Pen -ArgumentList @(
            [System.Drawing.Color]::FromArgb(50, 20, 20, 70), $lw)
        $spen.LineJoin = $pen.LineJoin
        $spen.StartCap = $pen.StartCap
        $spen.EndCap = $pen.EndCap
        $g.TranslateTransform((P 3 $s), (P 5 $s))
        $g.DrawPath($spen, $cPath)
        $g.DrawPath($spen, $uPath)
        $g.DrawLine($spen, (P 222 $s), (P 60 $s), (P 222 $s), (P 200 $s))
        $g.ResetTransform()
        $spen.Dispose()
    }
    $g.DrawPath($pen, $cPath)
    $g.DrawPath($pen, $uPath)
    $g.DrawLine($pen, (P 222 $s), (P 60 $s), (P 222 $s), (P 200 $s))
    $pen.Dispose()

    $g.Dispose()
    return $bmp
}

$images = @{}
foreach ($sz in $sizes) {
    $bmp = New-IconBitmap $sz
    [byte[]]$pngBytes = Encode-PngBytes $bmp
    $images[$sz] = $pngBytes
    $bmp.Dispose()
}

$ico = Join-Path $outDir 'cui_app.ico'
$fs = [System.IO.File]::Create($ico)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
foreach ($sz in $sizes) {
    $dim = if ($sz -ge 256) { 0 } else { $sz }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$images[$sz].Length)
    $bw.Write([UInt32]$offset)
    $offset += $images[$sz].Length
}
foreach ($sz in $sizes) { $bw.Write($images[$sz]) }
$bw.Flush()
$bw.Dispose()

$prev = Join-Path (Split-Path -Parent $outDir) 'build'
if (Test-Path $prev) {
    foreach ($pv in $sizes) {
        $ms = New-Object System.IO.MemoryStream -ArgumentList @($images[$pv], $false)
        $img = [System.Drawing.Image]::FromStream($ms)
        $z = if ($pv -eq 16) { 12 } else { 4 }
        $zoom = New-Object System.Drawing.Bitmap(($pv * $z), ($pv * $z))
        $zg = [System.Drawing.Graphics]::FromImage($zoom)
        $zg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $zg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $zg.DrawImage($img, 0, 0, $zoom.Width, $zoom.Height)
        $zoom.Save((Join-Path $prev "app_icon_${pv}.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $zg.Dispose()
        $zoom.Dispose()
        $img.Dispose()
    }
}

"icon written: $ico ({0} bytes)" -f (Get-Item $ico).Length
