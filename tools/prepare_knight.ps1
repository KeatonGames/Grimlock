# Rebuild a playable hero without changing the original KayKit sources.
param([ValidateSet('Knight', 'Ranger', 'Mage')][string]$Character = 'Knight')
$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot '../game/assets'
function Read-Glb($path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $length = [BitConverter]::ToInt32($bytes, 12)
    $json = [Text.Encoding]::UTF8.GetString($bytes, 20, $length) | ConvertFrom-Json
    $binOffset = 20 + $length
    $binLength = [BitConverter]::ToInt32($bytes, $binOffset)
    return @{ json = $json; bin = [byte[]]$bytes[($binOffset + 8)..($binOffset + 7 + $binLength)] }
}
$knight = Read-Glb (Join-Path $root "heroes/$($Character.ToLowerInvariant())/base.glb")
$target = $knight.json
$binary = New-Object IO.MemoryStream
$binary.Write($knight.bin, 0, $knight.bin.Length)
function Append-Data($source, [byte[]]$bytes) {
    while ($binary.Length % 4) { $binary.WriteByte(0) }
    $offset = $binary.Length
    $viewBase = $target.bufferViews.Count
    $accessorBase = $target.accessors.Count
    foreach ($view in $source.bufferViews) {
        $view | Add-Member byteOffset ($offset + $view.byteOffset) -Force
        $view.buffer = 0
        $target.bufferViews += $view
    }
    foreach ($accessor in $source.accessors) {
        if ($null -ne $accessor.bufferView) { $accessor.bufferView += $viewBase }
        $target.accessors += $accessor
    }
    $binary.Write($bytes, 0, $bytes.Length)
    return $accessorBase
}
$names = @{}
$parents = @{}
for ($i = 0; $i -lt $target.nodes.Count; $i++) {
    $names[$target.nodes[$i].name] = $i
    foreach ($child in $target.nodes[$i].children) { $parents[$target.nodes[$child].name] = $target.nodes[$i].name }
}
$target | Add-Member animations @() -Force
$clips = [ordered]@{
    General = @('Idle_A', 'Hit_A', 'Death_A')
    MovementBasic = @('Walking_A', 'Running_A')
}
if ($Character -eq 'Knight') {
    $clips.CombatMelee = @('Melee_1H_Attack_Slice_Horizontal')
} elseif ($Character -eq 'Ranger') {
    $clips.MovementAdvanced = @('Running_HoldingBow')
    $clips.CombatRanged = @('Ranged_Bow_Idle', 'Ranged_Bow_Aiming_Idle', 'Ranged_Bow_Draw', 'Ranged_Bow_Release')
} else {
    $clips.CombatRanged = @('Ranged_Magic_Shoot')
}
foreach ($group in $clips.Keys) {
    $source = Read-Glb (Join-Path $root "heroes/other/animations/medium/Rig_Medium_$group.glb")
    $sourceParents = @{}
    foreach ($node in $source.json.nodes) {
        foreach ($child in $node.children) { $sourceParents[$source.json.nodes[$child].name] = $node.name }
    }
    $base = Append-Data $source.json $source.bin
    foreach ($name in $clips[$group]) {
        $clip = @($source.json.animations | Where-Object name -EQ $name)
        if ($clip.Count -ne 1) { throw "Missing or ambiguous clip $name" }
        $clip = $clip[0]
        foreach ($channel in $clip.channels) {
            $node = $source.json.nodes[$channel.target.node]
            if (!$names.ContainsKey($node.name)) { throw "Unmapped animation node $($node.name)" }
            if ($parents[$node.name] -ne $sourceParents[$node.name]) { throw "Incompatible parent for $($node.name)" }
            # Local bind transforms must agree before sharing animation channels.
            $bind = $target.nodes[$names[$node.name]]
            foreach ($property in @('translation', 'rotation', 'scale')) {
                $a = $node.$property; $b = $bind.$property
                if ($null -eq $a) { $a = switch ($property) { translation { @(0,0,0) } rotation { @(0,0,0,1) } scale { @(1,1,1) } } }
                if ($null -eq $b) { $b = switch ($property) { translation { @(0,0,0) } rotation { @(0,0,0,1) } scale { @(1,1,1) } } }
                for ($c = 0; $c -lt $a.Count; $c++) {
                    if ([Math]::Abs($a[$c] - $b[$c]) -gt 0.001) { throw "Bind mismatch: $($node.name) $property" }
                }
            }
            $channel.target.node = $names[$node.name]
        }
        # Navigation owns the root transform; retain animated hips and limbs.
        $clip.channels = @($clip.channels | Where-Object { $target.nodes[$_.target.node].name -ne 'root' })
        foreach ($sampler in $clip.samplers) { $sampler.input += $base; $sampler.output += $base }
        $target.animations += $clip
        $inputAccessor = $target.accessors[$clip.samplers[0].input]
        Write-Output "$name duration: $($inputAccessor.max)"
    }
}
$weapon = switch ($Character) { Knight { 'sword_1handed' } Ranger { 'bow_withString' } Mage { 'staff' } }
$socket = if ($Character -eq 'Ranger') { 'handslot.l' } else { 'handslot.r' }
$weaponRelative = switch ($Character) {
    Knight { 'towers/weapons/kaykit/sword_1handed.glb' }
    Ranger { 'heroes/ranger/props/bow_withString.glb' }
    Mage { 'heroes/mage/props/staff.glb' }
}
$weaponModel = Read-Glb (Join-Path $root $weaponRelative)
$weaponAsset = $weaponModel.json
$base = Append-Data $weaponAsset $weaponModel.bin
# Weapons share the matching character atlas.
$material = -1
for ($i = 0; $i -lt $target.materials.Count; $i++) {
    if ($target.materials[$i].name -eq $Character.ToLowerInvariant()) { $material = $i }
}
if ($material -lt 0) { throw "$Character material missing" }
foreach ($mesh in $weaponAsset.meshes) {
    foreach ($primitive in $mesh.primitives) {
        foreach ($attribute in $primitive.attributes.PSObject.Properties) { $attribute.Value += $base }
        foreach ($morph in $primitive.targets) {
            foreach ($attribute in $morph.PSObject.Properties) { $attribute.Value += $base }
        }
        $primitive.indices += $base
        $primitive.material = $material
    }
}
$nodeIndex = $target.nodes.Count
$target.nodes += [pscustomobject]@{ name = $(if ($Character -eq 'Knight') { 'Knight_Sword' } else { "$Character`_Weapon" }); mesh = $target.meshes.Count }
$target.meshes += $weaponAsset.meshes
$slot = $target.nodes[$names[$socket]]
$slot | Add-Member children @(@($slot.children) + @($nodeIndex) | Where-Object { $null -ne $_ }) -Force
if ($Character -eq 'Ranger') {
    function Add-Scalars([float[]]$values) {
        while ($binary.Length % 4) { $binary.WriteByte(0) }
        $view = $target.bufferViews.Count
        $target.bufferViews += [pscustomobject]@{ buffer = 0; byteOffset = $binary.Length; byteLength = 4 * $values.Count }
        foreach ($value in $values) {
            $bytes = [BitConverter]::GetBytes($value)
            $binary.Write($bytes, 0, 4)
        }
        $index = $target.accessors.Count
        $target.accessors += [pscustomobject]@{
            bufferView = $view; componentType = 5126; count = $values.Count; type = 'SCALAR'
            min = @([float]($values | Measure-Object -Minimum).Minimum)
            max = @([float]($values | Measure-Object -Maximum).Maximum)
        }
        return $index
    }
    foreach ($name in @('Ranged_Bow_Draw', 'Ranged_Bow_Release')) {
        $clip = $target.animations | Where-Object name -EQ $name
        $duration = $target.accessors[$clip.samplers[0].input].max[0]
        $times = if ($name -eq 'Ranged_Bow_Draw') { @(0, $duration) } else { @(0, 0.2, $duration) }
        $weights = if ($name -eq 'Ranged_Bow_Draw') { @(0, 1) } else { @(1, 0, 0) }
        $inputIndex = Add-Scalars $times
        $outputIndex = Add-Scalars $weights
        $samplerIndex = $clip.samplers.Count
        $clip.samplers += [pscustomobject]@{ input = $inputIndex; output = $outputIndex; interpolation = 'LINEAR' }
        $clip.channels += [pscustomobject]@{ sampler = $samplerIndex; target = [pscustomobject]@{ node = $nodeIndex; path = 'weights' } }
    }
}
while ($binary.Length % 4) { $binary.WriteByte(0) }
$target.buffers = @([pscustomobject]@{ byteLength = $binary.Length })
$jsonBytes = [Text.Encoding]::UTF8.GetBytes(($target | ConvertTo-Json -Depth 100 -Compress))
$jsonLength = [int]([Math]::Ceiling($jsonBytes.Length / 4) * 4)
$output = [IO.File]::Create((Join-Path $root "heroes/$($Character.ToLowerInvariant())/$($Character.ToLowerInvariant()).glb"))
$writer = New-Object IO.BinaryWriter($output)
try {
    $writer.Write([uint32]0x46546C67); $writer.Write([uint32]2)
    $writer.Write([uint32](12 + 8 + $jsonLength + 8 + $binary.Length))
    $writer.Write([uint32]$jsonLength); $writer.Write([uint32]0x4E4F534A); $writer.Write($jsonBytes)
    for ($i = $jsonBytes.Length; $i -lt $jsonLength; $i++) { $writer.Write([byte]32) }
    $writer.Write([uint32]$binary.Length); $writer.Write([uint32]0x004E4942); $writer.Write($binary.ToArray())
} finally { $writer.Dispose(); $binary.Dispose() }





