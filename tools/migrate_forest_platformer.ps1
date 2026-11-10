$ErrorActionPreference = 'Stop'
$path = Join-Path $PSScriptRoot '../game/assets/scenes/forest_defense.json'
$scene = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
$prefix = 'environment/nature/kenney/'
$mapping = @{
    'cliff_blockHalf_rock' = 'block-grass-low'; 'cliff_block_rock' = 'block-grass'
    'cliff_blockSlope_rock' = 'block-grass-large-slope'
    'ground_grass' = 'block-grass-low'; 'ground_pathEnd' = 'block-grass-low'
    'ground_pathStraight' = 'block-grass-low'; 'ground_pathBend' = 'block-grass-low'
    'rock_largeA' = 'rocks'; 'rock_largeB' = 'rocks'
    'tree_default' = 'tree'; 'tree_oak' = 'tree'; 'tree_pineTallA' = 'tree-pine'
    'plant_bush' = 'plant'; 'plant_bushSmall' = 'plant'; 'flower_yellowA' = 'flowers'
    'mushroom_redGroup' = 'mushrooms'; 'platform_grass' = 'block-grass-low'
    'tent_detailedOpen' = 'chest'; 'sign' = 'sign'; 'statue_column' = 'poles'; 'statue_columnDamaged' = 'poles'
}
$objects = @()
$details = @()
$detailId = 200
foreach ($object in $scene.objects) {
    if (!$object.model -or !$object.model.StartsWith($prefix)) { $objects += $object; continue }
    if ($object.name.StartsWith('Trail soil')) { continue }
    $old = [IO.Path]::GetFileNameWithoutExtension($object.model)
    if (!$mapping.ContainsKey($old)) { throw "Unmapped Nature Kit asset: $old" }
    $object.model = 'environment/platformer/' + $mapping[$old] + '.glb'
    $transform = $object.transform
    if ($old -eq 'cliff_blockSlope_rock') {
        # Original footprint: one unit across, six/two metres deep after scaling.
        # The new slope's top plane is y = 0.508 - tan(15 degrees) * z.
        # Reverse its facing and fit that plane to the existing ramp endpoints.
        $rise = [double]$transform.scale[1]
        # Overlap the rounded ends with adjacent blocks so their bevels do not leave a navigation seam.
        $transform.scale = @(($transform.scale[0] / 2), ($rise / (2 * [Math]::Tan([Math]::PI / 12))), ($transform.scale[2] / 2 + 0.2))
        # Reach across the adjacent block's raised grass lip, then step down onto its flat top.
        $transform.scale[1] *= 1.08
        $transform.position[1] = -(0.508 - [Math]::Tan([Math]::PI / 12)) * $transform.scale[1]
        if ($object.id -eq '64') { $transform.rotation = @(0,1,0,0) }
        else { $transform.rotation = @(0,0.7071067811865476,0,0.7071067811865476) }
    } elseif ($old.StartsWith('ground_')) {
        $transform.position[1] = -1
        if ($old.StartsWith('ground_path')) {
            $details += [pscustomobject]@{
                id = [string]$detailId; name = 'Trail stepping stones'; parent = '1'
                model = 'environment/platformer/stones.glb'; role = 'grimlock.decoration'
                transform = @{ position = @($transform.position[0],0.012,$transform.position[2]);
                    rotation = $transform.rotation; scale = @(1.8,1,1.8) }
            }
            ++$detailId
        }
    } else {
        $transform.position[1] = [double]$transform.position[1] - 0.05 * $transform.scale[1]
        if ($old.StartsWith('tree_')) {
            $transform.scale = @($transform.scale | ForEach-Object { $_ * 0.85 })
        }
        if ($old -eq 'platform_grass') { $transform.position[1] -= 0.9 }
        if ($old -eq 'tent_detailedOpen') { $object.name = 'Woodland supply chest' }
        if ($old.StartsWith('statue_column')) { $object.name = 'Objective boundary posts' }
    }
    if ($object.role -eq 'grimlock.terrain') {
        if ($old -ne 'cliff_blockSlope_rock') {
            # Rounded grass lips must overlap; isolated two-metre tiles disconnect large agents.
            $transform.scale[0] *= 1.25
            $transform.scale[2] *= 1.25
        } elseif ($object.id -eq '64') { $transform.scale[0] *= 1.25 }
    }
    $objects += $object
}
$scene.objects = @($objects) + $details
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('{')
foreach ($property in $scene.PSObject.Properties) {
    if ($property.Name -eq 'objects') { continue }
    $lines.Add('  "' + $property.Name + '": ' + (ConvertTo-Json -InputObject $property.Value -Depth 30 -Compress) + ',')
}
$lines.Add('  "objects": [')
for ($i = 0; $i -lt $scene.objects.Count; ++$i) {
    $suffix = if ($i -lt $scene.objects.Count - 1) { ',' } else { '' }
    $lines.Add('    ' + (ConvertTo-Json -InputObject $scene.objects[$i] -Depth 30 -Compress) + $suffix)
}
$lines.Add('  ]'); $lines.Add('}')
[IO.File]::WriteAllLines($path, $lines)
