param([Parameter(Mandatory = $true)][string]$ZipPath)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$assetRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../game/assets'))
$archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
try {
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName -notmatch '^Models/GLB format/[^/]+\.glb$') { continue }
        $name = [IO.Path]::GetFileName($entry.FullName)
        $category = 'environment/platformer'
        if ($name.StartsWith('character-')) { $category = 'heroes/other/platformer' }
        elseif ($name -eq 'bomb.glb') { $category = 'towers/projectiles/platformer' }
        $directory = Join-Path $assetRoot $category
        [IO.Directory]::CreateDirectory($directory) | Out-Null
        $source = [IO.MemoryStream]::new()
        $stream = $entry.Open()
        try { $stream.CopyTo($source) } finally { $stream.Dispose() }
        $bytes = $source.ToArray()
        $source.Dispose()
        $jsonLength = [BitConverter]::ToInt32($bytes, 12)
        $gltf = [Text.Encoding]::UTF8.GetString($bytes, 20, $jsonLength) | ConvertFrom-Json
        $binaryLength = [BitConverter]::ToInt32($bytes, 20 + $jsonLength)
        $binary = [IO.MemoryStream]::new()
        $writer = [IO.BinaryWriter]::new($binary)
        try {
            $writer.Write($bytes, 28 + $jsonLength, $binaryLength)
            foreach ($image in $gltf.images) {
                if (!$image.uri) { continue }
                if ($image.uri -ne 'Textures/colormap.png') { throw "Unexpected image URI: $($image.uri)" }
                while ($binary.Position % 4) { $writer.Write([byte]0) }
                $offset = $binary.Position
                $texture = $archive.GetEntry('Models/GLB format/Textures/colormap.png').Open()
                try { $texture.CopyTo($binary) } finally { $texture.Dispose() }
                $image.PSObject.Properties.Remove('uri')
                $image | Add-Member -NotePropertyName bufferView -NotePropertyValue $gltf.bufferViews.Count
                $image | Add-Member -NotePropertyName mimeType -NotePropertyValue 'image/png' -Force
                $gltf.bufferViews += [pscustomobject]@{ buffer = 0; byteOffset = $offset; byteLength = $binary.Position - $offset }
            }
            $gltf.buffers[0].byteLength = $binary.Length
            while ($binary.Position % 4) { $writer.Write([byte]0) }
            $json = [Text.Encoding]::UTF8.GetBytes(($gltf | ConvertTo-Json -Depth 100 -Compress))
            $padding = (4 - $json.Length % 4) % 4
            $output = [IO.MemoryStream]::new()
            $result = [IO.BinaryWriter]::new($output)
            try {
                $result.Write([uint32]0x46546C67); $result.Write([uint32]2)
                $result.Write([uint32](28 + $json.Length + $padding + $binary.Length))
                $result.Write([uint32]($json.Length + $padding)); $result.Write([uint32]0x4E4F534A)
                $result.Write($json)
                for ($i = 0; $i -lt $padding; ++$i) { $result.Write([byte]32) }
                $result.Write([uint32]$binary.Length); $result.Write([uint32]0x004E4942)
                $result.Write($binary.ToArray())
                [IO.File]::WriteAllBytes((Join-Path $directory $name), $output.ToArray())
            } finally { $result.Dispose(); $output.Dispose() }
        } finally { $writer.Dispose(); $binary.Dispose() }
    }
    [IO.Compression.ZipFileExtensions]::ExtractToFile(
        $archive.GetEntry('License.txt'), (Join-Path $assetRoot 'licenses/kenney_platformer_LICENSE.txt'), $true)
} finally { $archive.Dispose() }
