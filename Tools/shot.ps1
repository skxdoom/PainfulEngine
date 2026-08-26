# Converts a PainfulEngine .tga screenshot to a downscaled .png for review.
#
# Image review cost scales with PIXEL COUNT, so half-size is a 4x saving and is
# plenty for judging geometry, culling, colour and layout. Pass -Full for the
# native 1280x720 when fine texture detail is the actual question, or -Crop
# "x,y,w,h" to look at one region.
param(
    [Parameter(Mandatory = $true)][string]$Tga,
    [string]$Out = "",
    [double]$Scale = 0.5,
    [switch]$Full,
    [string]$Crop = ""
)

Add-Type -AssemblyName System.Drawing

if (-not $Out) { $Out = [System.IO.Path]::ChangeExtension($Tga, ".png") }
if ($Full) { $Scale = 1.0 }

# The engine writes uncompressed 32-bit BGRA TGAs, top-down (desc bit 5).
$bytes = [System.IO.File]::ReadAllBytes($Tga)
$w = [BitConverter]::ToUInt16($bytes, 12)
$h = [BitConverter]::ToUInt16($bytes, 14)
$depth = $bytes[16]
$topDown = ($bytes[17] -band 0x20) -ne 0
if ($depth -ne 32) { throw "expected a 32-bit TGA, got $depth-bit" }

$src = New-Object System.Drawing.Bitmap ($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
$data = $src.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $src.PixelFormat)
$stride = $data.Stride
$row = New-Object byte[] ($w * 4)
for ($y = 0; $y -lt $h; $y++) {
    $srcY = if ($topDown) { $y } else { $h - 1 - $y }
    [Array]::Copy($bytes, 18 + $srcY * $w * 4, $row, 0, $w * 4)
    [System.Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]($data.Scan0.ToInt64() + $y * $stride), $w * 4)
}
$src.UnlockBits($data)

# Crop first so the scale applies to the region actually being reviewed.
if ($Crop) {
    $c = $Crop -split ','
    $cropRect = New-Object System.Drawing.Rectangle ([int]$c[0]), ([int]$c[1]), ([int]$c[2]), ([int]$c[3])
    $cropped = $src.Clone($cropRect, $src.PixelFormat)
    $src.Dispose()
    $src = $cropped
}

$dw = [int][Math]::Round($src.Width * $Scale)
$dh = [int][Math]::Round($src.Height * $Scale)
$dst = New-Object System.Drawing.Bitmap ($dw, $dh)
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($src, 0, 0, $dw, $dh)
$g.Dispose()
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output ("{0}  {1}x{2}" -f $Out, $dw, $dh)
$dst.Dispose()
$src.Dispose()
