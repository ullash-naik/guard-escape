# unify_skins.ps1 - makes a multi-skin glTF character loadable by raylib.
#
# raylib only reads skins[0]. Mixamo characters converted with FBX2glTF often
# have one skin per clothing piece, and no single skin holds every bone. This
# script:
#   1. builds ONE skin containing the union of all joints (largest skin first)
#      and copies each joint's inverse bind matrix into the binary chunk;
#   2. rewrites every mesh's JOINTS_0 data so its bone indices point into the
#      unified joint list;
#   3. removes the other skins, so the file is a plain single-skin glTF.
# Convert with FBX2glTF --skinning-weights 4 first (raylib reads 4 weights).
param([Parameter(Mandatory = $true)][string]$Glb)

$bytes = [System.IO.File]::ReadAllBytes($Glb)
$jsonLen = [BitConverter]::ToUInt32($bytes, 12)
$json = [System.Text.Encoding]::UTF8.GetString($bytes, 20, $jsonLen) | ConvertFrom-Json
$binStart = 20 + $jsonLen + 8
$binLen = [BitConverter]::ToUInt32($bytes, 20 + $jsonLen)

$skins = @($json.skins)
if ($skins.Count -le 1) { Write-Output "  $(Split-Path $Glb -Leaf): single skin, nothing to do"; exit 0 }

# --- 1. union of joints, largest skin first (skins with real bind matrices first) ---
$identity = New-Object byte[] 64
foreach ($k in 0, 5, 10, 15) { [Array]::Copy([BitConverter]::GetBytes([single]1.0), 0, $identity, 4 * $k, 4) }
$order = 0..($skins.Count - 1) | Sort-Object { -@($skins[$_].joints).Count }, { [int]($null -eq $skins[$_].inverseBindMatrices) }
$joints = New-Object System.Collections.Generic.List[int]
$ibm = @{}
foreach ($s in $order) {
    $sk = $skins[$s]
    $hasIbm = $null -ne $sk.inverseBindMatrices
    if ($hasIbm) {
        $acc = $json.accessors[[int]$sk.inverseBindMatrices]
        $bv = $json.bufferViews[[int]$acc.bufferView]
        $off = $binStart + [int]($bv.byteOffset) + [int]($acc.byteOffset)
    }
    $list = @($sk.joints)
    for ($i = 0; $i -lt $list.Count; $i++) {
        $j = [int]$list[$i]
        if (-not $ibm.ContainsKey($j)) {
            $m = New-Object byte[] 64
            if ($hasIbm) { [Array]::Copy($bytes, $off + 64 * $i, $m, 0, 64) } else { [Array]::Copy($identity, $m, 64) }
            $ibm[$j] = $m
            $joints.Add($j)
        }
    }
}

# --- 2. rewrite JOINTS_0 of every skinned primitive (in place, in $bytes) ---
$done = @{}
$changed = 0
foreach ($node in $json.nodes) {
    if ($null -eq $node.mesh -or $null -eq $node.skin) { continue }
    $own = @($skins[[int]$node.skin].joints)
    $map = foreach ($j in $own) { $joints.IndexOf([int]$j) }
    foreach ($prim in $json.meshes[[int]$node.mesh].primitives) {
        $ai = $prim.attributes.JOINTS_0
        if ($null -eq $ai -or $done.ContainsKey([int]$ai)) { continue }
        $done[[int]$ai] = $true
        $acc = $json.accessors[[int]$ai]
        $bv = $json.bufferViews[[int]$acc.bufferView]
        $size = if ($acc.componentType -eq 5121) { 1 } else { 2 }
        $stride = if ($bv.byteStride) { [int]$bv.byteStride } else { 4 * $size }
        $base = $binStart + [int]($bv.byteOffset) + [int]($acc.byteOffset)
        for ($v = 0; $v -lt [int]$acc.count; $v++) {
            for ($c = 0; $c -lt 4; $c++) {
                $p = $base + $v * $stride + $c * $size
                $old = if ($size -eq 1) { $bytes[$p] } else { [BitConverter]::ToUInt16($bytes, $p) }
                $new = if ($old -lt $map.Count -and $map[$old] -ge 0) { $map[$old] } else { 0 }
                if ($size -eq 1) { $bytes[$p] = [byte]$new } else { $bytes[$p] = [byte]($new -band 0xFF); $bytes[$p + 1] = [byte]($new -shr 8) }
            }
        }
        $changed++
    }
}

# --- 3. append the unified inverse bind matrices and keep only one skin ---
$pad = (4 - ($binLen % 4)) % 4
$newBin = New-Object System.IO.MemoryStream
$newBin.Write($bytes, $binStart, $binLen)
$newBin.Write((New-Object byte[] $pad), 0, $pad)
$ibmOffset = $binLen + $pad
foreach ($j in $joints) { $newBin.Write($ibm[$j], 0, 64) }
$binBytes = $newBin.ToArray()

$bvIndex = @($json.bufferViews).Count
$json.bufferViews += [pscustomobject]@{ buffer = 0; byteOffset = $ibmOffset; byteLength = 64 * $joints.Count }
$accIndex = @($json.accessors).Count
$json.accessors += [pscustomobject]@{ bufferView = $bvIndex; componentType = 5126; count = $joints.Count; type = "MAT4" }
$json.buffers[0].byteLength = $binBytes.Length
$json.skins = @([pscustomobject]@{ name = "unified"; joints = $joints.ToArray(); inverseBindMatrices = $accIndex })
foreach ($node in $json.nodes) { if ($null -ne $node.skin) { $node.skin = 0 } }

# --- write the new .glb ---
$jsonText = $json | ConvertTo-Json -Depth 100 -Compress
$jsonBytes = [System.Text.Encoding]::UTF8.GetBytes($jsonText)
$jpad = (4 - ($jsonBytes.Length % 4)) % 4
$jsonChunk = $jsonBytes + ([byte[]](,0x20 * $jpad))
$bpad = (4 - ($binBytes.Length % 4)) % 4
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$total = 12 + 8 + $jsonChunk.Length + 8 + $binBytes.Length + $bpad
$w.Write([uint32]0x46546C67); $w.Write([uint32]2); $w.Write([uint32]$total)
$w.Write([uint32]$jsonChunk.Length); $w.Write([uint32]0x4E4F534A); $w.Write([byte[]]$jsonChunk)
$w.Write([uint32]($binBytes.Length + $bpad)); $w.Write([uint32]0x004E4942); $w.Write([byte[]]$binBytes)
if ($bpad) { $w.Write((New-Object byte[] $bpad)) }
$w.Flush()
[System.IO.File]::WriteAllBytes($Glb, $out.ToArray())
Write-Output "  $(Split-Path $Glb -Leaf): $($skins.Count) skins -> 1 skin with $($joints.Count) joints, $changed meshes rewritten"
