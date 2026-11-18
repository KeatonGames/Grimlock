$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$author = Join-Path $repoRoot 'out/build/debug/Debug/GreenhavenAuthor.exe'

# The explicit authoring tool uses Engine's heightfield implementation for grounded placement.
# Regeneration overwrites scene edits; normal editing should use GrimlockEditor.
cmake --build --preset debug --target GreenhavenAuthor --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Could not build GreenhavenAuthor.' }
& $author (Join-Path $repoRoot 'game/assets')
if ($LASTEXITCODE -ne 0) { throw 'Could not author Greenhaven Valley.' }
