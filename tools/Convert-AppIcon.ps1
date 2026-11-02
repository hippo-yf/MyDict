param(
 [string]$Source = (Join-Path $PSScriptRoot '../assets/app-icon.png'),
 [string]$Destination = (Join-Path $PSScriptRoot '../assets/app-icon.ico')
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$original=[Drawing.Image]::FromFile([IO.Path]::GetFullPath($Source))
try {
 $frames=@()
 foreach($size in @(16,20,24,32,40,48,64,128,256)) {
  $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics=[Drawing.Graphics]::FromImage($bitmap)
  $stream=[IO.MemoryStream]::new()
  try {
   $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
   $graphics.CompositingQuality=[Drawing.Drawing2D.CompositingQuality]::HighQuality
   $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
   $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
   $graphics.DrawImage($original,[Drawing.Rectangle]::new(0,0,$size,$size))
   $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
   $frames+=@{Size=$size;Bytes=$stream.ToArray()}
  } finally {$graphics.Dispose();$bitmap.Dispose();$stream.Dispose()}
 }
 $output=[IO.File]::Create([IO.Path]::GetFullPath($Destination));$writer=[IO.BinaryWriter]::new($output)
 try {
  $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$frames.Count)
  $offset=6+16*$frames.Count
  foreach($frame in $frames){$dimension=if($frame.Size -eq 256){0}else{$frame.Size};$writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0);$writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$frame.Bytes.Length);$writer.Write([uint32]$offset);$offset+=$frame.Bytes.Length}
  foreach($frame in $frames){$writer.Write([byte[]]$frame.Bytes)}
 } finally {$writer.Dispose();$output.Dispose()}
 Write-Output ('Created multi-resolution ICO: '+$Destination)
} finally {$original.Dispose()}
