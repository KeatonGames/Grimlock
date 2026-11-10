# Derive grounded tower models from the supplied Kenney GLBs; retain embedded materials/textures.
$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot '../game/assets'
function Write-Glb($path, $json, [byte[]]$binary) {
    $data = [Text.Encoding]::UTF8.GetBytes(($json | ConvertTo-Json -Depth 80 -Compress))
    $padding = (4 - $data.Length % 4) % 4
    $stream = [IO.MemoryStream]::new(); $writer = [IO.BinaryWriter]::new($stream)
    $writer.Write([uint32]0x46546C67); $writer.Write([uint32]2)
    $writer.Write([uint32](28 + $data.Length + $padding + $binary.Length))
    $writer.Write([uint32]($data.Length + $padding)); $writer.Write([uint32]0x4E4F534A); $writer.Write($data)
    for ($i=0; $i -lt $padding; ++$i) { $writer.Write([byte]32) }
    $writer.Write([uint32]$binary.Length); $writer.Write([uint32]0x004E4942); $writer.Write($binary)
    [IO.File]::WriteAllBytes((Join-Path $root $path), $stream.ToArray())
    $writer.Dispose(); $stream.Dispose()
}
function Ground-Tower($source, $output, $scale, $centerHeight, [double]$yaw = 0) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $root $source))
    $length = [BitConverter]::ToInt32($bytes, 12)
    $json = [Text.Encoding]::UTF8.GetString($bytes, 20, $length) | ConvertFrom-Json
    $binLength = [BitConverter]::ToInt32($bytes, 20 + $length)
    $binary = [byte[]]$bytes[(28+$length)..(27+$length+$binLength)]
    $scene = $json.scenes[$json.scene]
    $index = $json.nodes.Count
    $angle = $yaw * [Math]::PI / 360
    $json.nodes += @{name='Grounded tower';children=@($scene.nodes);scale=@($scale,$scale,$scale)
        translation=@(0,(-$centerHeight),0);rotation=@(0,([Math]::Sin($angle)),0,([Math]::Cos($angle)))}
    $scene.nodes = @($index)
    Write-Glb $output $json $binary
}
# The supplied balcony points along +X; gameplay construction facing uses +Z for both towers.
Ground-Tower 'towers/weapons/castle/siege-tower.glb' 'towers/structures/archer_tower.glb' 1.65 0.75 -90
Ground-Tower 'towers/structures/pirate/tower-complete-small.glb' 'towers/structures/mage_tower.glb' 0.594 0.6

# A faceted luminous core with swept flame tongues. Local +Z points toward the target.
$binary = [IO.MemoryStream]::new(); $writer = [IO.BinaryWriter]::new($binary)
$views=@(); $accessors=@(); $primitives=@()
function Add-Part($triangles, $material) {
    $offset = $binary.Position
    $minimum=@([double]::MaxValue,[double]::MaxValue,[double]::MaxValue)
    $maximum=@([double]::MinValue,[double]::MinValue,[double]::MinValue)
    foreach ($point in $triangles) {
        for ($axis=0; $axis -lt 3; ++$axis) {
            $writer.Write([single]$point[$axis])
            $minimum[$axis]=[Math]::Min($minimum[$axis],$point[$axis]); $maximum[$axis]=[Math]::Max($maximum[$axis],$point[$axis])
        }
    }
    $script:views += @{buffer=0;byteOffset=$offset;byteLength=($binary.Position-$offset)}
    $script:accessors += @{bufferView=($views.Count-1);componentType=5126;count=$triangles.Count;type='VEC3';min=$minimum;max=$maximum}
    $script:primitives += @{attributes=@{POSITION=($accessors.Count-1)};material=$material}
}
$sphere=[Collections.Generic.List[object]]::new()
function Sphere-Point($latitude, $longitude) {
    $a=$latitude*[Math]::PI/8; $b=$longitude*2*[Math]::PI/12
    return ,@(([Math]::Sin($a)*[Math]::Cos($b)),([Math]::Cos($a)),([Math]::Sin($a)*[Math]::Sin($b)))
}
for($lat=0;$lat -lt 8;++$lat) { for($lon=0;$lon -lt 12;++$lon) {
    $a=Sphere-Point $lat $lon; $b=Sphere-Point ($lat+1) $lon
    $c=Sphere-Point ($lat+1) ($lon+1); $d=Sphere-Point $lat ($lon+1)
    if($lat -gt 0){$sphere.Add($a);$sphere.Add($b);$sphere.Add($d)}
    if($lat -lt 7){$sphere.Add($d);$sphere.Add($b);$sphere.Add($c)}
} }
Add-Part $sphere 0
$flames=[Collections.Generic.List[object]]::new()
for($i=0;$i -lt 7;++$i){
    $angle=$i*2*[Math]::PI/7
    $x=[Math]::Cos($angle)*0.7; $y=[Math]::Sin($angle)*0.7
    $tip=@(($x*1.25),($y*1.25),(-2.4-($i%3)*0.45))
    $a=@(($x-0.28),$y,-0.3); $b=@($x,($y+0.28),-0.3); $c=@(($x+0.28),$y,-0.3); $d=@($x,($y-0.28),-0.3)
    foreach($p in @($a,$b,$tip,$b,$c,$tip,$c,$d,$tip,$d,$a,$tip)){$flames.Add($p)}
}
Add-Part $flames 1
$materials=@()
foreach($color in @(@(0.7,0.16,1,1),@(0.4,0.02,0.9,1))){
    $materials+=@{doubleSided=$true;extensions=@{KHR_materials_unlit=@{}};pbrMetallicRoughness=@{baseColorFactor=$color;metallicFactor=0;roughnessFactor=1}}
}
$json=@{asset=@{version='2.0';generator='Grimlock purple fireball'};scene=0;scenes=@(@{nodes=@(0)});nodes=@(@{mesh=0;name='Purple fireball'})
    meshes=@(@{primitives=$primitives});materials=$materials;extensionsUsed=@('KHR_materials_unlit');buffers=@(@{byteLength=$binary.Length});bufferViews=$views;accessors=$accessors}
Write-Glb 'towers/projectiles/fireball_purple.glb' $json $binary.ToArray()
$writer.Dispose();$binary.Dispose()
