using namespace System.Numerics
$ErrorActionPreference = 'Stop'
$directory = Join-Path $PSScriptRoot '../game/assets/environment/graveyard'
function Write-Detail([string]$name, $points, $faces, $color, $emission) {
    $binary = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($binary)
    foreach ($face in $faces) {
        foreach ($index in $face) {
            $p = $points[$index]
            $writer.Write($p.X); $writer.Write($p.Y); $writer.Write($p.Z)
        }
    }
    $normalOffset = $binary.Position
    foreach ($face in $faces) {
        $normal = [Vector3]::Normalize([Vector3]::Cross(
            ($points[$face[1]]-$points[$face[0]]), ($points[$face[2]]-$points[$face[0]])))
        foreach ($index in $face) {
            $writer.Write($normal.X); $writer.Write($normal.Y); $writer.Write($normal.Z)
        }
    }
    $count = $faces.Count*3
    $xs = @($points | ForEach-Object {$_.X}); $ys = @($points | ForEach-Object {$_.Y}); $zs = @($points | ForEach-Object {$_.Z})
    $gltf = @{
        asset=@{version='2.0';generator='Grimlock level scenery details'}
        scene=0; scenes=@(@{nodes=@(0)}); nodes=@(@{mesh=0;name=$name})
        extensionsUsed=@('KHR_materials_unlit')
        meshes=@(@{primitives=@(@{attributes=@{POSITION=0;NORMAL=1};material=0})})
        materials=@(@{doubleSided=$true;extensions=@{KHR_materials_unlit=@{}};emissiveFactor=$emission
            pbrMetallicRoughness=@{baseColorFactor=$color;metallicFactor=0;roughnessFactor=1}})
        buffers=@(@{byteLength=$binary.Length})
        bufferViews=@(@{buffer=0;byteOffset=0;byteLength=$normalOffset},
                      @{buffer=0;byteOffset=$normalOffset;byteLength=$normalOffset})
        accessors=@(@{bufferView=0;componentType=5126;count=$count;type='VEC3'
            min=@(($xs|Measure-Object -Minimum).Minimum,($ys|Measure-Object -Minimum).Minimum,($zs|Measure-Object -Minimum).Minimum)
            max=@(($xs|Measure-Object -Maximum).Maximum,($ys|Measure-Object -Maximum).Maximum,($zs|Measure-Object -Maximum).Maximum)},
            @{bufferView=1;componentType=5126;count=$count;type='VEC3'})
    }
    $json = [Text.Encoding]::UTF8.GetBytes(($gltf | ConvertTo-Json -Depth 40 -Compress))
    $padding = (4-$json.Length%4)%4
    $output = [IO.MemoryStream]::new()
    $result = [IO.BinaryWriter]::new($output)
    $result.Write([uint32]0x46546C67); $result.Write([uint32]2)
    $result.Write([uint32](28+$json.Length+$padding+$binary.Length))
    $result.Write([uint32]($json.Length+$padding)); $result.Write([uint32]0x4E4F534A); $result.Write($json)
    for ($i=0; $i -lt $padding; ++$i) {$result.Write([byte]32)}
    $result.Write([uint32]$binary.Length); $result.Write([uint32]0x004E4942); $result.Write($binary.ToArray())
    [IO.File]::WriteAllBytes((Join-Path $directory ($name+'.glb')), $output.ToArray())
    $result.Dispose(); $output.Dispose(); $writer.Dispose(); $binary.Dispose()
}

# Arched light surfaces sit behind the stone frames rather than covering them with rectangles.
$points = [Collections.Generic.List[Vector3]]::new()
$points.Add([Vector3]::new(0,0.65,0))
$points.Add([Vector3]::new(-0.6,0,0)); $points.Add([Vector3]::new(0.6,0,0))
for ($i=0; $i -le 12; ++$i) {
    $angle=$i*[Math]::PI/12
    $points.Add([Vector3]::new((0.6*[Math]::Cos($angle)),(1+0.6*[Math]::Sin($angle)),0))
}
$faces = @()
for ($i=1; $i -lt $points.Count; ++$i) { $next=if($i+1 -lt $points.Count){$i+1}else{1}; $faces+=,@(0,$i,$next) }
Write-Detail 'door_glow_warm' $points $faces @(1,0.65,0.03,1) @(1,0.8,0.01)
Write-Detail 'portal_glow_red' $points $faces @(1,0.015,0.005,1) @(1,0.01,0)

$points=@([Vector3]::new(0,0,0),[Vector3]::new(-0.2,0.22,0),[Vector3]::new(0,0.2,0.17),
          [Vector3]::new(0.16,0.23,0),[Vector3]::new(0,0.19,-0.17),[Vector3]::new(0.08,0.8,0))
$faces=@(@(0,2,1),@(0,3,2),@(0,4,3),@(0,1,4),@(5,1,2),@(5,2,3),@(5,3,4),@(5,4,1))
Write-Detail 'brazier_flame' $points $faces @(1,0.3,0.005,1) @(1,0.6,0.005)
