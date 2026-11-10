using namespace System.Numerics

$ErrorActionPreference = 'Stop'
# A broad, faceted arrowhead and narrow shaft, pointing along the projectile's +Z axis.
$points = @(
    [Vector3]::new(0, 0, 0.9),
    [Vector3]::new(-0.3, 0, 0.05),
    [Vector3]::new(0, 0.1, 0.15),
    [Vector3]::new(0.3, 0, 0.05),
    [Vector3]::new(0, -0.1, 0.15),
    [Vector3]::new(-0.035, -0.035, 0.15),
    [Vector3]::new(0.035, -0.035, 0.15),
    [Vector3]::new(0.035, 0.035, 0.15),
    [Vector3]::new(-0.035, 0.035, 0.15),
    [Vector3]::new(-0.035, -0.035, -0.65),
    [Vector3]::new(0.035, -0.035, -0.65),
    [Vector3]::new(0.035, 0.035, -0.65),
    [Vector3]::new(-0.035, 0.035, -0.65)
)
$faces = @(@(0,1,2), @(0,2,3), @(0,3,4), @(0,4,1), @(1,4,2), @(2,4,3),
    @(5,9,6), @(6,9,10), @(6,10,7), @(7,10,11), @(7,11,8), @(8,11,12),
    @(8,12,5), @(5,12,9), @(9,12,10), @(10,12,11))
$data = [IO.MemoryStream]::new()
$writer = [IO.BinaryWriter]::new($data)
foreach ($face in $faces) {
    foreach ($index in $face) {
        $point = $points[$index]
        $writer.Write($point.X); $writer.Write($point.Y); $writer.Write($point.Z)
    }
}
$normalOffset = $data.Position
foreach ($face in $faces) {
    $normal = [Vector3]::Normalize([Vector3]::Cross(($points[$face[1]] - $points[$face[0]]), ($points[$face[2]] - $points[$face[0]])))
    foreach ($index in $face) {
        $writer.Write($normal.X); $writer.Write($normal.Y); $writer.Write($normal.Z)
    }
}
$count = $faces.Count * 3
$gltf = @{
    asset = @{ version = '2.0'; generator = 'Grimlock white arrow generator' }
    scene = 0; scenes = @(@{ nodes = @(0) }); nodes = @(@{ mesh = 0 })
    meshes = @(@{ primitives = @(@{ attributes = @{ POSITION = 0; NORMAL = 1 }; material = 0 }) })
    materials = @(@{ doubleSided = $true; emissiveFactor = @(0.35,0.35,0.35)
        pbrMetallicRoughness = @{ baseColorFactor = @(1,1,1,1); metallicFactor = 0; roughnessFactor = 0.55 } })
    buffers = @(@{ byteLength = $data.Length })
    bufferViews = @(@{ buffer = 0; byteOffset = 0; byteLength = $normalOffset },
                    @{ buffer = 0; byteOffset = $normalOffset; byteLength = $normalOffset })
    accessors = @(@{ bufferView = 0; componentType = 5126; count = $count; type = 'VEC3'; min = @(-0.3,-0.1,-0.65); max = @(0.3,0.1,0.9) },
                  @{ bufferView = 1; componentType = 5126; count = $count; type = 'VEC3' })
}
$json = [Text.Encoding]::UTF8.GetBytes(($gltf | ConvertTo-Json -Depth 30 -Compress))
$padding = (4 - $json.Length % 4) % 4
$output = [IO.MemoryStream]::new()
$result = [IO.BinaryWriter]::new($output)
$result.Write([uint32]0x46546C67); $result.Write([uint32]2)
$result.Write([uint32](28 + $json.Length + $padding + $data.Length))
$result.Write([uint32]($json.Length + $padding)); $result.Write([uint32]0x4E4F534A)
$result.Write($json)
for ($i = 0; $i -lt $padding; ++$i) { $result.Write([byte]32) }
$result.Write([uint32]$data.Length); $result.Write([uint32]0x004E4942); $result.Write($data.ToArray())
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot '../game/assets/towers/projectiles/arrow_white.glb'), $output.ToArray())
$result.Dispose(); $output.Dispose(); $writer.Dispose(); $data.Dispose()
