# make_icon.ps1 - build res\cui_markdown.ico (multi-size, PNG encoded)
#
# Design: rounded square (blue -> violet gradient) + white "M" polyline with a
# downward arrow underneath, i.e. the Markdown mark "M-down", fitting a Reader.
# Every size is redrawn from proportions (not a scaled down bitmap) and small
# sizes get a thicker stroke so the M stays legible at 16px.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File res\make_icon.ps1
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads BOM-less UTF-8
# as the local ANSI codepage; non-ASCII comments then corrupt parsing.
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'
$outDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sizes = @(16, 32, 48, 96, 128)

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
# GDI+ writes a plain PNG (fixed filter, default zlib). For a vertical
# gradient the "Up" filter turns almost the whole plate into zeros, which
# deflates far better, so the icon shrinks a lot for free.

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
            # 6 bits per RGB channel: invisible on a smooth gradient, but it
            # makes neighbouring rows identical far more often, which the Up
            # filter + deflate then collapse into runs of zeros. Alpha stays
            # at 8 bits so edges keep their antialiasing.
            $rgba[$dst + $i] = $raw[$src + $i + 2] -band 0xFC
            $rgba[$dst + $i + 1] = $raw[$src + $i + 1] -band 0xFC
            $rgba[$dst + $i + 2] = $raw[$src + $i] -band 0xFC
            $rgba[$dst + $i + 3] = $raw[$src + $i + 3]
        }
    }

    # Up filter everywhere except the first row.
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
    $ihdr[8] = 8      # bit depth
    $ihdr[9] = 6      # RGBA
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

    # Backplate: rounded square with a vertical gradient.
    $bx = [single]($s * 8)
    $bs = [single]($s * 240)
    $rad = [single]($s * 58)
    $rect = [System.Drawing.RectangleF]::new($bx, $bx, $bs, $bs)
    $path = New-RoundedPath $bx $bx $bs $bs $rad
    $c1 = [System.Drawing.Color]::FromArgb(255, 82, 126, 255)
    $c2 = [System.Drawing.Color]::FromArgb(255, 124, 74, 255)
    $lg = New-Object System.Drawing.Drawing2D.LinearGradientBrush -ArgumentList @($rect, $c1, $c2, [single]90)
    $g.FillPath($lg, $path)

    # Faint top highlight so the plate does not read as flat fill.
    $ha = [System.Drawing.Color]::FromArgb(46, 255, 255, 255)
    $hb = [System.Drawing.Color]::FromArgb(0, 255, 255, 255)
    $hl = New-Object System.Drawing.Drawing2D.LinearGradientBrush -ArgumentList @($rect, $ha, $hb, [single]90)
    $g.FillPath($hl, $path)

    $white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)

    # 16/20 px have no room for M + arrow: the arrow collapses into a blob
    # that touches the M. Those sizes keep the M only, drawn larger.
    $compact = $size -le 20
    if ($compact) {
        $pw = [single]($s * 34)
        if ($pw -lt 2.0) { $pw = [single]2.0 }
        $pen = New-Object System.Drawing.Pen -ArgumentList @($white, $pw)
        $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $m = @(
            [System.Drawing.PointF]::new((P 56 $s), (P 168 $s)),
            [System.Drawing.PointF]::new((P 56 $s), (P 88 $s)),
            [System.Drawing.PointF]::new((P 128 $s), (P 156 $s)),
            [System.Drawing.PointF]::new((P 200 $s), (P 88 $s)),
            [System.Drawing.PointF]::new((P 200 $s), (P 168 $s))
        )
        $g.DrawLines($pen, [System.Drawing.PointF[]]$m)
        $g.Dispose()
        return $bmp
    }

    # M: polyline with round joins; thicken proportionally on small sizes.
    $pw = [single]($s * 26)
    if ($pw -lt 2.0) { $pw = [single]2.0 }
    $pen = New-Object System.Drawing.Pen -ArgumentList @($white, $pw)
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $m = @(
        [System.Drawing.PointF]::new((P 76 $s), (P 140 $s)),
        [System.Drawing.PointF]::new((P 76 $s), (P 52 $s)),
        [System.Drawing.PointF]::new((P 128 $s), (P 132 $s)),
        [System.Drawing.PointF]::new((P 180 $s), (P 52 $s)),
        [System.Drawing.PointF]::new((P 180 $s), (P 140 $s))
    )
    $g.DrawLines($pen, [System.Drawing.PointF[]]$m)

    # Arrow: stem plus triangle, meeting the bottom of the M.
    $aw = [single]($s * 18)
    if ($aw -lt 1.5) { $aw = [single]1.5 }
    $apen = New-Object System.Drawing.Pen -ArgumentList @($white, $aw)
    $g.DrawLine($apen, (P 128 $s), (P 158 $s), (P 128 $s), (P 186 $s))
    $tri = @(
        [System.Drawing.PointF]::new((P 128 $s), (P 212 $s)),
        [System.Drawing.PointF]::new((P 103 $s), (P 178 $s)),
        [System.Drawing.PointF]::new((P 153 $s), (P 178 $s))
    )
    $gp = New-Object System.Drawing.Drawing2D.GraphicsPath
    $gp.AddPolygon([System.Drawing.PointF[]]$tri)
    $wb = New-Object System.Drawing.SolidBrush -ArgumentList @($white)
    $g.FillPath($wb, $gp)

    $g.Dispose()
    return $bmp
}

$images = @{}
$prevDir = Join-Path (Split-Path -Parent $outDir) 'build'   # scratch dir, not shipped
foreach ($sz in $sizes) {
    $bmp = New-IconBitmap $sz
    # NB: a byte[] returned from a function is unrolled into object[]; cast it
    # back or BinaryWriter picks the single-byte overload.
    [byte[]]$pngBytes = Encode-PngBytes $bmp
    $images[$sz] = $pngBytes
    if ($sz -eq 128 -and (Test-Path $prevDir)) {
        # preview what actually gets embedded (after the colour quantisation)
        $pms = New-Object System.IO.MemoryStream -ArgumentList @($pngBytes, $false)
        $pimg = [System.Drawing.Image]::FromStream($pms)
        $pimg.Save((Join-Path $prevDir 'icon_preview_128.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        $pimg.Dispose()
    }
    $bmp.Dispose()
}

$ico = Join-Path $outDir 'cui_markdown.ico'
$fs = [System.IO.File]::Create($ico)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)                 # reserved
$bw.Write([UInt16]1)                 # type = icon
$bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
foreach ($sz in $sizes) {
    $dim = if ($sz -ge 256) { 0 } else { $sz }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)               # palette
    $bw.Write([Byte]0)               # reserved
    $bw.Write([UInt16]1)             # planes
    $bw.Write([UInt16]32)            # bpp
    $bw.Write([UInt32]$images[$sz].Length)
    $bw.Write([UInt32]$offset)
    $offset += $images[$sz].Length
}
foreach ($sz in $sizes) { $bw.Write($images[$sz]) }
$bw.Flush()
$bw.Dispose()

# Small sizes blown up with nearest neighbour, for eyeballing legibility.
if (Test-Path $prevDir) {
    foreach ($pv in @(16, 32)) {
        $ms2 = New-Object System.IO.MemoryStream -ArgumentList @($images[$pv], $false)
        $b16 = [System.Drawing.Image]::FromStream($ms2)
        $zoom = New-Object System.Drawing.Bitmap(192, 192)
        $zg = [System.Drawing.Graphics]::FromImage($zoom)
        $zg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $zg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $zg.DrawImage($b16, 0, 0, 192, 192)
        $zoom.Save((Join-Path $prevDir "icon_preview_${pv}x.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        $zg.Dispose()
        $zoom.Dispose()
        $b16.Dispose()
    }
}

"icon written: $ico ({0} bytes)" -f (Get-Item $ico).Length
