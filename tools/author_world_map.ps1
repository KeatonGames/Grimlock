$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot '../game/assets'
$source=Get-Content (Join-Path $root 'scenes/forest_defense.json') -Raw|ConvertFrom-Json
$field=($source.objects|Where-Object heightfield).heightfield
$field.minimalistic=$false
$field.size=@(480,400); $field.resolution=@(241,201); $field.playable_size=@(90,70)
$field.pads=@(@{center=@(-10,0);radius=5;height=0},@{center=@(14,12);radius=5;height=1})
$field.noise_amplitude=0.12
$field.hills=@(@{center=@(-28,19);radius=14;height=8},@{center=@(24,23);radius=13;height=10},@{center=@(32,-12);radius=10;height=6},@{center=@(18,-12);radius=12;height=-2},@{center=@(24,-23);radius=9;height=-1.5})
$field.road=@(@(-10,0,0),@(-3,0,-3),@(5,0,0),@(14,1,12))
$objects=[Collections.Generic.List[object]]::new()
$objects.Add(@{id='1';name='Kingdom landscape';role='grimlock.terrain';heightfield=$field})
$script:mapId=2
function Add-Map($name,$model,$pos,$scale,$role='grimlock.decoration') {
 $objects.Add(@{id=[string]$script:mapId;name=$name;role=$role;model=$model;transform=@{position=$pos;scale=$scale;rotation=@(0,0,0,1)}}); ++$script:mapId
}
$level=Get-Content (Join-Path $root 'levels/forest_defense.json') -Raw|ConvertFrom-Json
Add-Map 'Greenhaven Valley' 'environment/graveyard/crypt-large.glb' @(-10,0,0) @(3,3,3) ('grimlock.map_location:'+$level.id)
Add-Map 'Greenhaven roof' 'environment/graveyard/crypt-large-roof.glb' @(-10,3,0) @(3,3,3)
$objects[$objects.Count-1].parent='2'; $objects[$objects.Count-1].transform.position=@(0,1,0); $objects[$objects.Count-1].transform.scale=@(1,1,1)
Add-Map 'Gravewatch Hollow' 'environment/graveyard/crypt-large.glb' @(14,1,12) @(2,2,2) 'grimlock.map_location:38d63a72-c95f-44be-85b1-e303f0c5e002'
$objects[$objects.Count-1].tint=@(0.72,0.76,0.95,1)
for($i=0;$i -lt 130;++$i) {
 $x=-36+72*(($i*0.61803398875)%1); $z=-23+48*(($i*0.41421356237)%1)
 if([Math]::Abs($x+10)-lt 6 -and [Math]::Abs($z)-lt 6){continue}
 if($z -gt 12){continue}
 if([Math]::Sqrt(($x-20)*($x-20)+($z+15)*($z+15))-lt 13){continue}
 Add-Map 'Kingdom pine' 'environment/platformer/tree-pine.glb' @($x,0,$z) @(2,2,2)
}
foreach($x in @(-27,-24,-21,24,28,32)) { Add-Map 'Mountain rocks' 'environment/platformer/rocks.glb' @($x,1,18) @(7,7,7) }
Add-Map 'Valley lake' 'towers/structures/primitives/cube.glb' @(18,-0.5,-12) @(12,0.05,9)
$objects[$objects.Count-1].tint=@(0.05,0.32,0.48,1)
Add-Map 'Eastern river' 'towers/structures/primitives/cube.glb' @(24,-0.5,-23) @(2,0.05,10)
$objects[$objects.Count-1].tint=@(0.05,0.32,0.48,1)
$objects[$objects.Count-1].transform.rotation=@(0,0.258819,0,0.965926)
foreach($p in @(@(12,1,10),@(17,1,13))) {
 Add-Map 'Village house' 'environment/graveyard/crypt-large.glb' $p @(1.2,1.2,1.2)
 Add-Map 'Village roof' 'environment/graveyard/crypt-large-roof.glb' @($p[0],2.2,$p[2]) @(1.2,1.2,1.2)
}
for($i=0;$i -lt 18;++$i) {
 $x=8+($i%6)*2.1; $z=7+[Math]::Floor($i/6)*3
 Add-Map 'Gravewatch map burial ground' 'environment/graveyard/gravestone-cross.glb' @($x,1,$z) @(1.4,1.4,1.4)
}
foreach($o in $objects) {
 if($o.name -eq 'Kingdom pine' -and $o.transform.position[0] -gt 6 -and $o.transform.position[2] -gt 4) {
  $o.tint=@(0.45,0.55,0.65,1)
 }
}
$scene=@{version=1;name='Kingdom World Map';active_camera='main';cameras=@(@{id='main';position=@(-32,43,-47);target=@(0,1,2);vertical_fov_degrees=42;far=250});lighting=$source.lighting;objects=@($objects.ToArray())}
$scene.lighting.points=@()
$scene|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root 'scenes/world_map.json')
