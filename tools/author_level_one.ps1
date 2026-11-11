$ErrorActionPreference = 'Stop'
$assetRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../game/assets'))
$objects = [Collections.Generic.List[object]]::new()
$nextId = 200
function Add-Object([string]$name, [string]$model, $position, $scale = @(1,1,1),
                    [double]$yaw = 0, [string]$role = 'grimlock.decoration',
                    [string]$parent = '', [string]$id = '', $tint = @(1,1,1,1)) {
    if (!$id) { $id = [string]$script:nextId; ++$script:nextId }
    $angle = $yaw * [Math]::PI / 360
    $object = [ordered]@{id=$id; name=$name}
    if ($parent) { $object.parent = $parent }
    if ($model) { $object.model = $model }
    $object.role = $role
    $object.transform = @{position=$position; scale=$scale; rotation=@(0,[Math]::Sin($angle),0,[Math]::Cos($angle))}
    $object.tint = $tint
    $objects.Add($object)
    return $id
}
function Add-Grass([string]$name, $position, [double]$width, [double]$depth, [double]$thickness) {
    $bottom = @($position[0], ($position[1]-$thickness), $position[2])
    $null = Add-Object $name 'environment/platformer/block-grass-large.glb' $bottom @(
        ($width/2), $thickness, ($depth/2)) 0 'grimlock.terrain' '1' -tint @(0.78,1,0.45,1)
}
function Add-Slope([string]$name, $position, [double]$width, [double]$depth, [double]$rise, [double]$yaw = 180) {
    $heightScale = $rise / (2 * [Math]::Tan([Math]::PI/12)) * 1.08
    $bottom = @($position[0], ($position[1] - (0.508-[Math]::Tan([Math]::PI/12))*$heightScale), $position[2])
    $null = Add-Object $name 'environment/platformer/block-grass-large-slope.glb' $bottom @(
        ($width/2), $heightScale, ($depth/2+0.25)) $yaw 'grimlock.terrain' '1' -tint @(0.78,1,0.45,1)
}
function Add-Prop([string]$name, [string]$file, $position, [double]$size = 1, [double]$yaw = 0) {
    $null = Add-Object $name ('environment/platformer/'+$file+'.glb') $position @($size,$size,$size) $yaw -parent '3'
}
function Add-Fence($position, [double]$yaw = 0, [double]$length = 3) {
    $null = Add-Object 'Low timber boundary fence' 'environment/platformer/fence-low-straight.glb' $position @(
        $length,2,1.3) $yaw -parent '3'
}
function Add-Zone([string]$name, $position, [string]$id, $lane) {
    $null = Add-Object $name '' $position @(1.3,1,1.3) 0 'grimlock.build_zone' -id $id
    $anchor = [string]([int]$id * 10 + 1)
    $point = [string]([int]$id * 10 + 2)
    $yaw = [Math]::Atan2(($lane[0]-$position[0]),($lane[1]-$position[2])) * 180 / [Math]::PI
    $null = Add-Object 'Construction anchor' '' @(0,0,0) @((1/1.3),1,(1/1.3)) 0 'grimlock.construction_anchor' $id -id $anchor
    $null = Add-Object 'Construction point' '' @(0,0,0) @(1,1,1) $yaw 'grimlock.construction_point' $anchor -id $point
}

$null = Add-Object 'Reference layout terrain' '' @(0,0,0) -id '1'
$null = Add-Object 'Wooden bridge' '' @(0,0,0) -id '2'
$null = Add-Object 'Woodland scenery' '' @(0,0,0) -id '3'
$null = Add-Object 'Mausoleum courtyard' '' @(0,0,0) -id '4'

# Hand-authored overlapping islands leave a real ravine beneath the bridge.
Add-Grass 'Entrance meadow' @(-8,0,-4) 12 8 1.25
Add-Grass 'Entrance bridge approach' @(-5,0,-0.5) 6 4 1.25
Add-Grass 'Eastern meadow' @(7,0,-1) 8 8 1.25
Add-Grass 'Eastern ramp approach' @(7,0,2) 7 3 1.25
Add-Grass 'Raised western garden' @(-7,1,5) 10 6 2.25
Add-Slope 'Western garden access slope' @(-8,0,0.5) 4 3 1
Add-Grass 'Mausoleum terrace' @(7.5,2,13) 11 8 3.25
Add-Slope 'Broad ascent to the mausoleum' @(7,0,6) 5.2 6 2

# A solid visible timber base supports the decorative boards, including their source-mesh gaps.
$null = Add-Object 'Solid bridge timber deck' 'towers/structures/primitives/cube.glb' @(1,-0.07,0) @(
    3.8,0.07,1.85) 0 'grimlock.terrain' '2' -tint @(0.51,0.27,0.11,1)
for ($board = 0; $board -lt 12; ++$board) {
    $x = -2.3 + $board * 0.6
    $null = Add-Object 'Bridge timber deck plank' 'environment/platformer/platform.glb' @($x,-0.045,0) @(
        0.65,0.6,4) 0 'grimlock.decoration' '2'
}
foreach ($x in @(-2.3,-0.1,2.1,4.3)) {
    foreach ($z in @(-1.85,1.85)) {
        $null = Add-Object 'Bridge support post' 'towers/structures/primitives/cube.glb' @($x,-0.7,$z) @(
            0.15,1,0.15) 0 -parent '2' -tint @(0.48,0.24,0.09,1)
        $null = Add-Object 'Bridge post cap' 'towers/structures/primitives/cube.glb' @($x,0.36,$z) @(
            0.22,0.12,0.22) 0 -parent '2' -tint @(0.85,0.51,0.2,1)
    }
}
foreach ($z in @(-1.85,1.85)) {
    $null = Add-Object 'Bridge edge beam' 'towers/structures/primitives/cube.glb' @(1,-0.01,$z) @(
        3.5,0.12,0.1) 0 -parent '2' -tint @(0.66,0.34,0.13,1)
}

Add-Zone 'Entrance meadow construction point' @(-5.5,0,-4.6) '108' @(-5,-1.5)
Add-Zone 'Raised western garden construction point' @(-7,1,4.6) '109' @(-3,0)
Add-Zone 'Eastern meadow construction point' @(7.5,0,-2.4) '110' @(5,0.5)
Add-Zone 'Mausoleum terrace construction point' @(5.8,2,10.8) '111' @(7,7.5)

# Portal opens toward the route; its glow is scenery and does not alter spawning.
foreach ($z in @(-4.9,-2.5)) {
    foreach ($y in @(0.3,0.9,1.5)) {
        $null = Add-Object 'Portal stone jamb' 'towers/structures/primitives/cube.glb' @(-12,$y,$z) @(
            0.45,0.29,0.34) 0 -parent '3' -tint @(0.5,0.48,0.68,1)
    }
}
for ($stone=0; $stone -le 8; ++$stone) {
    $angle=$stone*[Math]::PI/8
    $null = Add-Object 'Portal arch voussoir' 'towers/structures/primitives/cube.glb' @(
        -12,(1.5+1.2*[Math]::Sin($angle)),(-3.7+1.2*[Math]::Cos($angle))) @(
        0.45,0.33,0.27) 0 -parent '3' -tint @(0.55,0.52,0.74,1)
    $rotation=([Math]::PI/2-$angle)/2
    $objects[$objects.Count-1].transform.rotation=@([Math]::Sin($rotation),0,0,[Math]::Cos($rotation))
}
$null = Add-Object 'Red portal interior' 'environment/graveyard/portal_glow_red.glb' @(-12.15,0,-3.7) @(
    1.6,1.6,1.6) 90 -parent '3'
$null = Add-Object 'Enemy entrance banner' 'environment/castle/flag.glb' @(-12.7,0,-5.4) @(
    1.5,1.5,1.5) 90 -parent '3' -tint @(2,0.2,0.3,1)

# The raised sill sits on a visible foundation rather than hovering above the terrace.
$null = Add-Object 'Mausoleum stone foundation' 'towers/structures/primitives/cube.glb' @(8.5,2.06,14.2) @(
    1.68,0.06,1.81) 0 -parent '4' -tint @(0.55,0.53,0.7,1)
$null = Add-Object 'Defense objective - mausoleum' 'environment/graveyard/crypt-large.glb' @(8.5,2.12,14.2) @(
    1.8,2,1.5) 180 'grimlock.objective' '4' '105'
$null = Add-Object 'Mausoleum pitched roof' 'environment/graveyard/crypt-large-roof.glb' @(0,1,0) @(
    1,1,1) 0 -parent '105'
$null = Add-Object 'Warm mausoleum doorway' 'environment/graveyard/door_glow_warm.glb' @(0,0,1.21) @(
    0.5,0.5,1) 180 -parent '105'
# Low steps lead from the playable terrace to the raised door sill.
for ($step = 0; $step -lt 3; ++$step) {
    $null = Add-Object 'Mausoleum entrance step' 'towers/structures/primitives/cube.glb' @(8.5,(2.025+$step*0.04),(11.7+$step*0.28)) @(
        1.05,(0.025+$step*0.02),0.2) 0 -parent '4' -tint @(0.55,0.53,0.7,1)
}
foreach ($x in @(3.6,5.6,7.6,9.6,11.6)) {
    $null = Add-Object 'Courtyard rear stone wall' 'environment/graveyard/stone-wall.glb' @($x,2,16.4) @(
        2,1.1,1.5) 0 'grimlock.obstacle' '4'
}
foreach ($z in @(12.8,14.8)) {
    $null = Add-Object 'Courtyard east stone wall' 'environment/graveyard/stone-wall.glb' @(12.4,2,$z) @(
        2,1.1,1.5) 90 'grimlock.obstacle' '4'
}
foreach ($x in @(6.1,10.9)) {
    $null = Add-Object 'Mausoleum royal banner' 'environment/castle/flag.glb' @($x,2,14.2) @(
        1.6,1.6,1.6) 90 -parent '4' -tint @(1.5,0.55,1,1)
    $null = Add-Object 'Brazier stone plinth' 'towers/structures/primitives/cube.glb' @($x,2.35,12.2) @(
        0.38,0.35,0.38) 0 -parent '4' -tint @(0.55,0.53,0.7,1)
    $null = Add-Object 'Brazier fire basket' 'environment/graveyard/fire-basket.glb' @($x,2.7,12.2) @(
        1.7,1.7,1.7) 0 -parent '4'
    $null = Add-Object 'Golden brazier flame' 'environment/graveyard/brazier_flame.glb' @($x,2.85,12.2) @(
        1,1,1) 0 -parent '4'
}

# Frame the silhouettes, leaving routes and all four construction footprints clear.
foreach ($p in @(@(-12,0,-6.4),@(-11,1,6),@(-8.5,1,6.6),@(-3.8,1,7.1),@(3.4,2,14.8),
                  @(11.5,2,11.2),@(9.3,0,-0.3),@(9.8,0,-3.2))) {
    Add-Prop 'Woodland pine' 'tree-pine' $p 1.5
}
foreach ($p in @(@(-11.2,0,-0.6),@(-6.8,1,7),@(-3,1,5.8),@(-3.4,0,-6.5),
                  @(4.3,0,-3.7),@(9.4,0,2),@(3.4,2,12.4))) {
    Add-Prop 'Rounded boundary rocks' 'rocks' $p 2.3
}
Add-Fence @(-9.5,0,-7.2) 0 3.3
Add-Fence @(-6.1,0,-7.2) 0 3.3
Add-Fence @(-9,1,2.7) 0 3.2
Add-Fence @(-4.4,1,4.3) 90 2
Add-Fence @(7.8,0,-4.6) 0 3
Add-Fence @(10.6,0,1.2) 90 2.4
Add-Fence @(11,2,9.6) 0 3
Add-Prop 'Supply crate' 'crate' @(-8,0,-5.6) 1.7 10
Add-Prop 'Stacked supply crate' 'crate' @(-8.7,0,-4.9) 1.4 -12
Add-Prop 'Supply barrel' 'barrel' @(-7.2,0,-5.2) 1.5
foreach ($p in @(@(-13,0,-1.4),@(-10,0,-5.8),@(-5,0,-6.3),@(-5,1,6.8),@(-10.5,1,4.2),
                  @(5.4,0,-3.5),@(7.8,0,1.6),@(5.5,0,2),@(5,2,15.5),@(4,2,10))) {
    Add-Prop 'Woodland flowers' 'flowers' $p 1.2
    Add-Prop 'Grass clump' 'plant' @(($p[0]+0.6),$p[1],($p[2]-0.4)) 1.3 25
}
$null = Add-Object 'Dusk purple backdrop' 'towers/structures/primitives/cube.glb' @(0,-3.9,4) @(
    400,0.1,400) 0 -tint @(0.008,0.005,0.018,1)

# Orient the entrance at screen left and the mausoleum at screen right in the authored camera.
foreach ($object in $objects) {
    $object.transform.position[0] = -$object.transform.position[0]
    $object.transform.rotation[1] = -$object.transform.rotation[1]
}

$scene = [ordered]@{
    version=1; name='Level 01 - Forest Defense'
    cameras=@(@{id='main';position=@(-18,22,-25);target=@(0,0,3);vertical_fov_degrees=32;far=180})
    active_camera='main'
    lighting=@{directional=@{direction=@(-0.45,-0.85,-0.3);color=@(1,0.95,0.86);intensity=2.8}
        environment_intensity=0.85;exposure=1
        points=@(@{position=@(11.2,1.1,-3.7);color=@(1,0.035,0.015);intensity=8;range=4},
                 @{position=@(-8.5,3.1,12.2);color=@(1,0.55,0.08);intensity=7;range=3},
                 @{position=@(-6.1,3.1,12.2);color=@(1,0.45,0.06);intensity=5;range=3},
                 @{position=@(-10.9,3.1,12.2);color=@(1,0.45,0.06);intensity=5;range=3})}
    objects=@($objects.ToArray())
}
$scene | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $assetRoot 'scenes/forest_defense.json')
$levelPath = Join-Path $assetRoot 'levels/forest_defense.json'
$level = Get-Content -LiteralPath $levelPath -Raw | ConvertFrom-Json
$level.description = 'Cross the timber bridge, defend four construction points, and hold the walled mausoleum above the woodland meadow.'
$level.objective.position = @(-8.5,10.8)
$level.objective.height = 2
$level.hero_spawns = ,@(-7,2,11)
$level | Add-Member -NotePropertyName available_heroes -NotePropertyValue @() -Force
$level.spawns[0].minimum = @(9.3,-3.9)
$level.spawns[0].maximum = @(10.7,-3.1)
$level.spawns[1].minimum = @(10,-3.5)
$level.spawns[1].maximum = @(10,-3.5)
$level | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $levelPath
