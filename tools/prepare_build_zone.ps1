using namespace System.Numerics

$ErrorActionPreference = 'Stop'
$assetDirectory = Join-Path $PSScriptRoot '../game/assets/towers/structures/tower_defense'
$source = [IO.File]::ReadAllBytes((Join-Path $assetDirectory 'selection-a.glb'))
$jsonLength = [BitConverter]::ToUInt32($source, 12)
$gltf = [Text.Encoding]::UTF8.GetString($source, 20, $jsonLength) | ConvertFrom-Json
$binaryOffset = 28 + $jsonLength
$binaryLength = [BitConverter]::ToUInt32($source, (20 + $jsonLength))

$positions = [Collections.Generic.List[single]]::new()
$normals = [Collections.Generic.List[single]]::new()
$uvs = [Collections.Generic.List[single]]::new()
$indices = [Collections.Generic.List[uint16]]::new()

function New-Ring([single]$radius, [single]$chamfer, [single]$height) {
    $corner = $radius - $chamfer
    @(
        [Vector3]::new(-$corner, $height, -$radius)
        [Vector3]::new($corner, $height, -$radius)
        [Vector3]::new($radius, $height, -$corner)
        [Vector3]::new($radius, $height, $corner)
        [Vector3]::new($corner, $height, $radius)
        [Vector3]::new(-$corner, $height, $radius)
        [Vector3]::new(-$radius, $height, $corner)
        [Vector3]::new(-$radius, $height, -$corner)
    )
}

function Add-Quad([Vector3]$a, [Vector3]$b, [Vector3]$c, [Vector3]$d,
                  [Vector3]$desiredNormal, [single]$paletteV) {
    $normal = [Vector3]::Normalize([Vector3]::Cross(($b - $a), ($c - $a)))
    if ([Vector3]::Dot($normal, $desiredNormal) -lt 0) {
        $swap = $b
        $b = $d
        $d = $swap
        $normal = -$normal
    }
    $first = [uint16]($positions.Count / 3)
    foreach ($point in @($a, $b, $c, $d)) {
        $positions.AddRange([single[]]@($point.X, $point.Y, $point.Z))
        $normals.AddRange([single[]]@($normal.X, $normal.Y, $normal.Z))
        $uvs.AddRange([single[]]@(0.71875, $paletteV))
    }
    foreach ($offset in @(0, 1, 2, 0, 2, 3)) {
        $indices.Add([uint16]($first + $offset))
    }
}

# Match the selection corners' five-centimetre height, bevel and orange palette.
# The continuous top surface replaces selection-b's open vertical ribbon.
$outerBase = New-Ring 0.5 0.05 0
$outerSide = New-Ring 0.5 0.05 0.025
$outerTop = New-Ring 0.485 0.045 0.05
$innerTop = New-Ring 0.385 0.02 0.05
$innerSide = New-Ring 0.37 0.015 0.025
$innerBase = New-Ring 0.37 0.015 0
for ($i = 0; $i -lt 8; ++$i) {
    $next = ($i + 1) % 8
    $outward = [Vector3]::Normalize($outerBase[$i] + $outerBase[$next])
    $up = [Vector3]::UnitY
    Add-Quad $outerBase[$i] $outerBase[$next] $outerSide[$next] $outerSide[$i] $outward 0.675
    Add-Quad $outerSide[$i] $outerSide[$next] $outerTop[$next] $outerTop[$i] ($outward + $up) 0.625
    Add-Quad $outerTop[$i] $outerTop[$next] $innerTop[$next] $innerTop[$i] $up 0.575
    Add-Quad $innerTop[$i] $innerTop[$next] $innerSide[$next] $innerSide[$i] ($up - $outward) 0.625
    Add-Quad $innerSide[$i] $innerSide[$next] $innerBase[$next] $innerBase[$i] (-$outward) 0.675
    Add-Quad $innerBase[$i] $innerBase[$next] $outerBase[$next] $outerBase[$i] (-$up) 0.675
}

$binary = [IO.MemoryStream]::new()
$writer = [IO.BinaryWriter]::new($binary)
$writer.Write($source, $binaryOffset, $binaryLength)

function Add-Accessor($values, [string]$type, [int]$components, [bool]$indexData = $false) {
    $offset = $binary.Position
    foreach ($value in $values) {
        if ($indexData) { $writer.Write([uint16]$value) }
        else { $writer.Write([single]$value) }
    }
    $length = $binary.Position - $offset
    while ($binary.Position % 4) { $writer.Write([byte]0) }
    $view = $gltf.bufferViews.Count
    $gltf.bufferViews += [pscustomobject]@{buffer = 0; byteOffset = $offset; byteLength = $length}
    $accessor = [pscustomobject]@{
        bufferView = $view; componentType = $(if ($indexData) {5123} else {5126})
        count = [int]($values.Count / $components); type = $type
    }
    if ([object]::ReferenceEquals($values, $positions)) {
        $accessor | Add-Member -NotePropertyName min -NotePropertyValue @(-0.5, 0, -0.5)
        $accessor | Add-Member -NotePropertyName max -NotePropertyValue @(0.5, 0.05, 0.5)
    }
    $id = $gltf.accessors.Count
    $gltf.accessors += $accessor
    return $id
}

$positionAccessor = Add-Accessor $positions 'VEC3' 3
$normalAccessor = Add-Accessor $normals 'VEC3' 3
$uvAccessor = Add-Accessor $uvs 'VEC2' 2
$indexAccessor = Add-Accessor $indices 'SCALAR' 1 $true
$gltf.meshes[0].primitives = @([pscustomobject]@{
    attributes = [pscustomobject]@{POSITION = $positionAccessor; NORMAL = $normalAccessor; TEXCOORD_0 = $uvAccessor}
    indices = $indexAccessor; material = 0
})
$gltf.meshes[0].name = 'construction-zone'
$gltf.nodes[0].name = 'Construction_Zone'
$gltf.buffers[0].byteLength = $binary.Length
$writer.Flush()

$json = [Text.Encoding]::UTF8.GetBytes(($gltf | ConvertTo-Json -Depth 100 -Compress))
$padding = (4 - $json.Length % 4) % 4
$output = [IO.MemoryStream]::new()
$result = [IO.BinaryWriter]::new($output)
$result.Write([uint32]0x46546C67)
$result.Write([uint32]2)
$result.Write([uint32](28 + $json.Length + $padding + $binary.Length))
$result.Write([uint32]($json.Length + $padding))
$result.Write([uint32]0x4E4F534A)
$result.Write($json)
for ($i = 0; $i -lt $padding; ++$i) { $result.Write([byte]32) }
$result.Write([uint32]$binary.Length)
$result.Write([uint32]0x004E4942)
$result.Write($binary.ToArray())
$result.Flush()
[IO.File]::WriteAllBytes((Join-Path $assetDirectory 'construction-zone.glb'), $output.ToArray())
$result.Dispose()
$output.Dispose()
$writer.Dispose()
$binary.Dispose()

