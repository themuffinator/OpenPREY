param(
    [string]$InputPath = ".install\basepr\guis\assets\menu\background_tiled.tga",
    [string]$TileStem = "background",
    [string[]]$OutputDirs = @(
        "basepr\guis\assets\menu",
        ".install\basepr\guis\assets\menu"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-TgaImage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $Path))
    if ($bytes.Length -lt 18) {
        throw "TGA file '$Path' is too small."
    }

    $idLength = [int]$bytes[0]
    $imageType = [int]$bytes[2]
    $colorMapType = [int]$bytes[1]
    $width = [int]$bytes[12] + ([int]$bytes[13] -shl 8)
    $height = [int]$bytes[14] + ([int]$bytes[15] -shl 8)
    $pixelDepth = [int]$bytes[16]
    $imageDescriptor = [int]$bytes[17]

    if ($imageType -ne 2) {
        throw "Only uncompressed type-2 TGA images are supported. '$Path' is type $imageType."
    }
    if ($colorMapType -ne 0) {
        throw "Color-mapped TGA images are not supported. '$Path' uses a color map."
    }
    if ($pixelDepth -ne 24 -and $pixelDepth -ne 32) {
        throw "Only 24-bit and 32-bit TGA images are supported. '$Path' is ${pixelDepth}-bit."
    }

    $bytesPerPixel = [int]($pixelDepth / 8)
    $pixelDataOffset = 18 + $idLength
    $expectedDataSize = $width * $height * $bytesPerPixel
    if ($bytes.Length -lt ($pixelDataOffset + $expectedDataSize)) {
        throw "TGA file '$Path' is truncated."
    }

    $pixelData = New-Object byte[] $expectedDataSize
    [System.Buffer]::BlockCopy($bytes, $pixelDataOffset, $pixelData, 0, $expectedDataSize)

    return @{
        Width = $width
        Height = $height
        PixelDepth = $pixelDepth
        ImageDescriptor = $imageDescriptor
        BytesPerPixel = $bytesPerPixel
        PixelData = $pixelData
    }
}

function Write-TgaImage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [int]$Width,
        [Parameter(Mandatory = $true)]
        [int]$Height,
        [Parameter(Mandatory = $true)]
        [int]$PixelDepth,
        [Parameter(Mandatory = $true)]
        [int]$ImageDescriptor,
        [Parameter(Mandatory = $true)]
        [byte[]]$PixelData
    )

    $header = New-Object byte[] 18
    $header[2] = 2
    $header[12] = [byte]($Width -band 0xff)
    $header[13] = [byte](($Width -shr 8) -band 0xff)
    $header[14] = [byte]($Height -band 0xff)
    $header[15] = [byte](($Height -shr 8) -band 0xff)
    $header[16] = [byte]$PixelDepth
    $header[17] = [byte]$ImageDescriptor

    $directory = Split-Path -Parent $Path
    if ($directory) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    try {
        $stream.Write($header, 0, $header.Length)
        $stream.Write($PixelData, 0, $PixelData.Length)
    } finally {
        $stream.Dispose()
    }
}

function Copy-TgaRegion {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Image,
        [Parameter(Mandatory = $true)]
        [int]$X,
        [Parameter(Mandatory = $true)]
        [int]$Y,
        [Parameter(Mandatory = $true)]
        [int]$Width,
        [Parameter(Mandatory = $true)]
        [int]$Height
    )

    if ($X -lt 0 -or $Y -lt 0 -or ($X + $Width) -gt $Image.Width -or ($Y + $Height) -gt $Image.Height) {
        throw "Crop region $X,$Y $Width x $Height is outside the source image bounds."
    }

    $rowBytes = $Width * $Image.BytesPerPixel
    $output = New-Object byte[] ($rowBytes * $Height)

    for ($row = 0; $row -lt $Height; ++$row) {
        $sourceOffset = (($Y + $row) * $Image.Width + $X) * $Image.BytesPerPixel
        $destOffset = $row * $rowBytes
        [System.Buffer]::BlockCopy($Image.PixelData, $sourceOffset, $output, $destOffset, $rowBytes)
    }

    return $output
}

$image = Read-TgaImage -Path $InputPath

if (($image.Width % 3) -ne 0 -or ($image.Height % 3) -ne 0) {
    throw "Expected '$InputPath' to be a 3x3 expansion image, but dimensions are $($image.Width)x$($image.Height)."
}

$tileWidth = [int]($image.Width / 3)
$tileHeight = [int]($image.Height / 3)

$tiles = @(
    @{ Name = "${TileStem}_left.tga"; X = 0; Y = $tileHeight },
    @{ Name = "${TileStem}_right.tga"; X = $tileWidth * 2; Y = $tileHeight },
    @{ Name = "${TileStem}_top.tga"; X = $tileWidth; Y = $tileHeight * 2 },
    @{ Name = "${TileStem}_bottom.tga"; X = $tileWidth; Y = 0 }
)

foreach ($outputDir in $OutputDirs) {
    foreach ($tile in $tiles) {
        $croppedPixels = Copy-TgaRegion -Image $image -X $tile.X -Y $tile.Y -Width $tileWidth -Height $tileHeight
        $outputPath = Join-Path $outputDir $tile.Name
        Write-TgaImage -Path $outputPath -Width $tileWidth -Height $tileHeight -PixelDepth $image.PixelDepth -ImageDescriptor $image.ImageDescriptor -PixelData $croppedPixels
        Write-Output "Wrote $outputPath"
    }
}
