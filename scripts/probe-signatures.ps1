# probe-signatures.ps1
# Scans the installed Deadlock server.dll / engine2.dll for known byte
# signatures and reports unique/missing/ambiguous matches, plus a 64-byte hex
# window at the first four hits. These are file offsets, not runtime addresses.
# A unique match does not prove the function's ABI or live behavior.
# Read-only: no game process or installation state is modified.

[CmdletBinding()]
param(
    [string]$GameDir = $env:DEADLOCK_DIR,
    [string[]]$SignatureName,
    [switch]$Json
)

$ErrorActionPreference = "Stop"
if (-not $GameDir) {
    $configPath = Join-Path $PSScriptRoot "../.tmp/playtest/config.json"
    if (Test-Path $configPath) {
        $GameDir = (Get-Content -Raw $configPath | ConvertFrom-Json).game_dir
    }
}
if (-not $GameDir) { throw "Supply -GameDir or configure win-playtest first." }
if (-not $Json) { Write-Output "Scanning binaries under: $GameDir" }
# engine2 lives under game\bin\win64; the game server modules live one
# level deeper under game\citadel\bin\win64.
$binDirs = @(
    (Join-Path $gameDir "game\bin\win64"),
    (Join-Path $gameDir "game\citadel\bin\win64")
)

function Find-Library([string]$lib) {
    foreach ($dir in $binDirs) {
        $path = Join-Path $dir $lib
        if (Test-Path $path) { return $path }
    }
    return $null
}

# Name, library, pattern ('?' or '??' = wildcard byte).
$signatures = @(
    @{ Name = "CMaterialSystem2AppSystemDict::OnAppSystemLoaded"; Lib = "engine2.dll"; Pattern = "40 53 48 83 EC 20 80 B9 ? ? ? ? ? 48 8B D9 74 ? 48 8B 89" },
    @{ Name = "CServerSideClientBase::FilterMessage";             Lib = "engine2.dll"; Pattern = "40 53 48 83 EC 30 48 8B 02 48 8B D9 48 8B CA FF 50 18 48 3B 05" },
    @{ Name = "CNetworkGameServerBase::ReplyConnection";          Lib = "engine2.dll"; Pattern = "48 8B C4 55 41 55 41 56" },
    @{ Name = "UTIL_Remove";                                      Lib = "server.dll";  Pattern = "48 85 C9 74 ? 48 8B D1 48 8B 0D" },
    @{ Name = "CBasePlayerController::SetPawn";                   Lib = "server.dll";  Pattern = "44 88 4C 24 ?? 53 57" },
    @{ Name = "CCitadelPlayerPawn::ModifyCurrency";               Lib = "server.dll";  Pattern = "48 89 5C 24 ?? 55 41 54 41 55 41 56 41 57 48 8D AC 24" },
    @{ Name = "CBaseEntity::TakeDamageOld";                       Lib = "server.dll";  Pattern = "40 55 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8D 6C 24 ?? 48 89 9D ?? ?? ?? ?? 45 33 ED" },

    @{ Name = "LookupVDataByHash"; Lib = "server.dll"; Pattern = "40 53 48 83 EC ?? 89 54 24 ?? 8B D9" },
    @{ Name = "CCitadelBaseAbility::SetUpgradeBits"; Lib = "server.dll"; Pattern = "48 8B C4 89 50 ?? 55 57 48 8D 68" },
    @{ Name = "CCitadelAbilityComponent::SwapItemSlots"; Lib = "server.dll"; Pattern = "66 41 3B D0 0F 84 ?? ?? ?? ?? 66 44 89 44 24 18 66 89 54 24 10 55 56 57" },
    # The pre-cast adapter must resolve uniquely before it can intercept item input.
    @{ Name = "CCitadelPlayerPawn::AbilityThink"; Lib = "server.dll"; Pattern = "40 55 53 41 54 41 55 41 57 48 8D AC 24" },
    # Inventory uses the engine's item lifecycle, not raw entity creation.
    @{ Name = "GetVDataInstanceByName"; Lib = "server.dll"; Pattern = "48 89 5C 24 ?? 57 48 83 EC ?? 33 FF 4C 8B CA 8B D9" },
    @{ Name = "CCitadelAbilityComponent::FindAbilityByName"; Lib = "server.dll"; Pattern = "40 57 48 83 EC ?? 48 8B F9 B9" },
    @{ Name = "CCitadelAbilityComponent::CreateAndRegisterAbility"; Lib = "server.dll"; Pattern = "48 89 5C 24 ?? 44 89 4C 24 ?? 55 56 57 41 56 41 57 48 83 EC" },
    @{ Name = "CCitadelAbilityComponent::OnAbilityRemoved"; Lib = "server.dll"; Pattern = "48 89 5C 24 ?? 55 56 57 48 83 EC 60 41 0F B6 E8 48 8B F2 48 8B F9" },
    @{ Name = "CCitadelPlayerPawn::AddItem"; Lib = "server.dll"; Pattern = "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 30 48 8B D9 41 8B F1 B9 04 00 00 00 41" },
    @{ Name = "CCitadelPlayerPawn::SellItem"; Lib = "server.dll"; Pattern = "40 55 53 41 54 41 55 41 57 48 8D 6C 24 80 48 81 EC 80 01 00 00 4C 8B E9 45 0F B6 E1 B9 04 00 00" },

    # Ghost-pawn creation chain and world-text lifecycle
    # targets, matching src/render/world_text_probes.cc.
    # CBaseEntity::Teleport has no byte
    # pattern here: it resolves as vtable slot 163 of CBaseEntity, not by
    # scanning.
    @{ Name = "CCitadelPlayerController::CreateHeroPawn";         Lib = "server.dll";  Pattern = "48 8B C4 48 89 48 ?? 55 57 48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 89 58 ?? 48 8B F9 48 8B 0D" },
    @{ Name = "CCitadelPlayerController::SpawnObserverPawn";      Lib = "server.dll";  Pattern = "48 8B C4 55 53 48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 89 78 ?? 4C 89 60" },
    @{ Name = "CCitadelPlayerPawn::SelectHeroInternal";           Lib = "server.dll";  Pattern = "40 55 41 54 41 55 41 56 48 8D 6C 24 88 48 81 EC 78 01 00 00 4C 8B E1 4C 8B EA" },
    @{ Name = "CBaseModelEntity::SetModel";                       Lib = "server.dll";  Pattern = "40 53 48 83 EC 20 48 8B D9 4C 8B C2 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 40 48 8B 01 FF 50 60 48 8B 54 24 40 48 8B CB E8 15 00 00 00" },
    @{ Name = "CEntitySystem::CreateEntityByName";                Lib = "server.dll";  Pattern = "48 83 EC ?? 48 8B 0D ?? ?? ?? ?? 41 8B C0" },
    @{ Name = "CEntitySystem::QueueSpawnEntity";                  Lib = "server.dll";  Pattern = "40 56 57 41 56 48 83 EC ?? F7 42" },
    @{ Name = "CEntitySystem::ExecuteQueuedCreation";             Lib = "server.dll";  Pattern = "48 89 5C 24 ?? 57 48 81 EC ?? ?? ?? ?? FF 81 ?? ?? ?? ?? 48 8D 44 24" },
    @{ Name = "CEntityInstance::AcceptInput";                     Lib = "server.dll";  Pattern = "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B F0 48 8B D9" }
)

function Format-HexWindow([byte[]]$data, [int]$offset, [int]$length) {
    $end = [Math]::Min($offset + $length, $data.Length)
    $parts = New-Object System.Collections.Generic.List[string]
    for ($i = $offset; $i -lt $end; $i++) {
        $parts.Add("{0:X2}" -f $data[$i])
    }
    return ($parts -join " ")
}

function Get-PatternBytes([string]$pattern, [ref]$mask) {
    $tokens = $pattern.Trim() -split "\s+"
    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($t in $tokens) {
        if ($t -eq "?" -or $t -eq "??") {
            $bytes.Add(0); $mask.Value.Add($false)
        } else {
            $bytes.Add([Convert]::ToByte($t, 16)); $mask.Value.Add($true)
        }
    }
    return ,$bytes.ToArray()
}

# Scan in compiled code: PowerShell byte-by-byte iteration over server.dll
# takes minutes. Return every hit, including overlaps, to expose ambiguity.
if (-not ("Modlock.SignatureProbe" -as [type])) {
    Add-Type -TypeDefinition @'
using System.Collections.Generic;
namespace Modlock {
    public static class SignatureProbe {
        public static int[] Find(byte[] data, byte[] pattern, bool[] mask) {
            var hits = new List<int>();
            for (int i = 0; i <= data.Length - pattern.Length; i++) {
                int j = 0;
                for (; j < pattern.Length; j++)
                    if (mask[j] && data[i + j] != pattern[j]) break;
                if (j == pattern.Length) hits.Add(i);
            }
            return hits.ToArray();
        }
    }
}
'@
}

if ($SignatureName) {
    foreach ($name in $SignatureName) {
        if ($name -notin $signatures.Name) { throw "Unknown signature: $name" }
    }
    $signatures = @($signatures | Where-Object { $_.Name -in $SignatureName })
}
$libraries = @{}
$records = @(foreach ($sig in $signatures) {
    if (-not $libraries.ContainsKey($sig.Lib)) {
        $path = Find-Library $sig.Lib
        if (-not $path) { throw "Missing $($sig.Lib) under $GameDir" }
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $hash = [System.Security.Cryptography.SHA256]::Create()
        try { $sha256 = [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
        finally { $hash.Dispose() }
        $libraries[$sig.Lib] = @{
            Path = $path
            Bytes = $bytes
            Sha256 = $sha256
        }
    }
    $library = $libraries[$sig.Lib]
    $mask = New-Object System.Collections.Generic.List[bool]
    $pat = Get-PatternBytes $sig.Pattern ([ref]$mask)
    $hits = [Modlock.SignatureProbe]::Find($library.Bytes, $pat, $mask.ToArray())
    $status = if ($hits.Count -eq 1) { "unique" } elseif ($hits.Count -eq 0) { "missing" } else { "ambiguous" }
    [pscustomobject]@{
        name = $sig.Name
        library = $library.Path
        sha256 = $library.Sha256
        pattern = $sig.Pattern
        status = $status
        file_offsets = @($hits)
        windows = @($hits | Select-Object -First 4 | ForEach-Object { Format-HexWindow $library.Bytes $_ 64 })
    }
})
if ($Json) {
    ConvertTo-Json -InputObject $records -Depth 4
} else {
    foreach ($record in $records) {
        Write-Output "[$($record.status.ToUpperInvariant())] $($record.name): $($record.file_offsets.Count) matches"
        for ($i = 0; $i -lt $record.windows.Count; $i++) {
            Write-Output ("[WINDOW] +0x{0:X}: {1}" -f $record.file_offsets[$i], $record.windows[$i])
        }
    }
}
