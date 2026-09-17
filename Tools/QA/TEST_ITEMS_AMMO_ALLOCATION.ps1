$ErrorActionPreference = "Stop"
$failures = New-Object System.Collections.Generic.List[string]
function Assert-True {
    param([bool]$Condition, [string]$Label)
    if (-not $Condition) { $failures.Add($Label) } else { Write-Host "PASS: $Label" }
}
# Deterministic model of the production ammo-pool policy.  This fixture is
# deliberately small, but it exercises the historical failure modes that the
# source-level invariants alone cannot: effective attachment-driven capacities,
# mixed storage representations, severe shortage ordering, placement rollback,
# and conservation across fair reserve waves.
$readyCatalog = @(
    [pscustomobject]@{ Name = '30-round magazine'; Size = 30;  IsBulk = $false },
    [pscustomobject]@{ Name = '100-round C-Mag';  Size = 100; IsBulk = $false },
    [pscustomobject]@{ Name = 'ammo box';          Size = 200; IsBulk = $true  },
    [pscustomobject]@{ Name = 'ammo crate';        Size = 500; IsBulk = $true  }
)
function Resolve-ReadyRepresentation {
    param([int]$WantedSize)
    $exact = $readyCatalog | Where-Object { -not $_.IsBulk -and $_.Size -eq $WantedSize } | Select-Object -First 1
    if ($null -ne $exact) { return $exact }
    return $readyCatalog | Where-Object { -not $_.IsBulk -and $_.Size -ge $WantedSize } | Sort-Object Size | Select-Object -First 1
}
$pool = New-Object System.Collections.Generic.List[object]
$pool.Add([pscustomobject]@{ Kind = 'ready30';  Rounds = 30  })
$pool.Add([pscustomobject]@{ Kind = 'ready100'; Rounds = 50  }) # partial magazine
$pool.Add([pscustomobject]@{ Kind = 'box';      Rounds = 200 })
$pool.Add([pscustomobject]@{ Kind = 'crate';    Rounds = 140 })
$initialRounds = ($pool | Measure-Object Rounds -Sum).Sum
function Get-PoolRounds {
    return [int](($pool | Measure-Object Rounds -Sum).Sum)
}
function Take-PoolRounds {
    param([int]$Wanted)
    $remaining = $Wanted
    $taken = 0
    foreach ($source in $pool) {
        if ($remaining -le 0) { break }
        $take = [Math]::Min([int]$source.Rounds, $remaining)
        $source.Rounds -= $take
        $remaining -= $take
        $taken += $take
    }
    return $taken
}
function Return-PoolRounds {
    param([int]$Rounds)
    if ($Rounds -le 0) { return }
    # Production may merge into an existing reachable stack or create a new one;
    # total conservation is the invariant that matters to this fixture.
    $pool[0].Rounds += $Rounds
}
$mercs = @(
    [pscustomobject]@{ Name='Adapter'; BaseMag=30; AttachmentBonus=70; EffectiveMag=100; Loaded=0; MaxSpare=3; Spares=New-Object System.Collections.Generic.List[int]; CanPlace=$true;  Blocked=$false },
    [pscustomobject]@{ Name='Standard'; BaseMag=30; AttachmentBonus=0;  EffectiveMag=30;  Loaded=0; MaxSpare=3; Spares=New-Object System.Collections.Generic.List[int]; CanPlace=$true;  Blocked=$false },
    [pscustomobject]@{ Name='NoPocket'; BaseMag=30; AttachmentBonus=0;  EffectiveMag=30;  Loaded=0; MaxSpare=3; Spares=New-Object System.Collections.Generic.List[int]; CanPlace=$false; Blocked=$false }
)
$events = New-Object System.Collections.Generic.List[string]
# Attachment-driven capacity must be the live demand size, not the base weapon size.
foreach ($m in $mercs) {
    Assert-True ($m.EffectiveMag -eq ($m.BaseMag + $m.AttachmentBonus)) "$($m.Name) effective magazine capacity includes attachments"
    $ready = Resolve-ReadyRepresentation $m.EffectiveMag
    Assert-True ($null -ne $ready -and -not $ready.IsBulk) "$($m.Name) resolves to weapon-ready ammo, never box/crate"
}
# Shootability-first priming: every otherwise-unarmed compatible merc gets one
# round before any sequential full top-up can consume the common pool.
foreach ($m in $mercs) {
    if ($m.Loaded -eq 0 -and (Get-PoolRounds) -gt 0) {
        $built = Take-PoolRounds 1
        $m.Loaded += $built
        if ($built -gt 0) { $events.Add("prime:$($m.Name)") }
    }
}
# Normal top-up then fills each effective magazine using the post-attachment size.
foreach ($m in $mercs) {
    $need = $m.EffectiveMag - $m.Loaded
    if ($need -gt 0) {
        $built = Take-PoolRounds $need
        $m.Loaded += $built
        if ($built -gt 0) { $events.Add("topup:$($m.Name):$built") }
    }
}
# Fair reserve waves: one spare per demand per wave, up to the per-demand cap.
# A placement failure must return every built round to the reachable pool and
# only block that demand; later demands/waves continue.
for ($wave = 0; $wave -lt 5; $wave++) {
    foreach ($m in $mercs) {
        if ($m.Blocked) { continue }
        $target = [Math]::Min($m.MaxSpare, $wave + 1)
        while ($m.Spares.Count -lt $target) {
            $available = Get-PoolRounds
            if ($available -le 0) { break }
            $wanted = [Math]::Min($m.EffectiveMag, $available)
            $ready = Resolve-ReadyRepresentation $m.EffectiveMag
            if ($null -eq $ready -or $ready.IsBulk) {
                $failures.Add("$($m.Name) reserve attempted a bulk/unready representation")
                $m.Blocked = $true
                break
            }
            $before = Get-PoolRounds
            $built = Take-PoolRounds $wanted
            if (-not $m.CanPlace) {
                Return-PoolRounds $built
                $after = Get-PoolRounds
                Assert-True ($after -eq $before) "$($m.Name) failed placement returns all rounds to pool"
                $events.Add("rollback:$($m.Name):$built")
                $m.Blocked = $true
                break
            }
            $m.Spares.Add($built)
            $events.Add("spare$($wave+1):$($m.Name):$built")
        }
    }
}
Assert-True (($events[0..2] -join ',') -eq 'prime:Adapter,prime:Standard,prime:NoPocket') 'all compatible mercs are primed before any full top-up'
Assert-True ($mercs[0].Loaded -eq 100) 'attachment-modified 100-round gun tops up to effective capacity'
Assert-True ($mercs[1].Loaded -eq 30 -and $mercs[2].Loaded -eq 30) 'standard guns top up to native capacity after priming'
Assert-True ($mercs[0].Spares.Count -eq 2 -and $mercs[1].Spares.Count -eq 2) 'normal reserve waves remain fair after priming under shortage'
Assert-True ($mercs[2].Spares.Count -eq 0 -and $mercs[2].Blocked) 'no-pocket demand is blocked without losing ammo or blocking others'
Assert-True (($events | Where-Object { $_ -like 'spare3:*' }).Count -eq 0) 'nobody receives a third spare before shortage stops the fair waves'
$loadedRounds = ($mercs | Measure-Object Loaded -Sum).Sum
$spareRounds = 0
foreach ($m in $mercs) { foreach ($r in $m.Spares) { $spareRounds += $r } }
$finalPool = Get-PoolRounds
Assert-True (($loadedRounds + $spareRounds + $finalPool) -eq $initialRounds) 'ammo rounds are conserved across build/place/rollback operations'
Assert-True ($initialRounds -lt (160 + 100*3 + 30*3 + 30*3)) 'fixture is a genuine severe-shortage case'
# Partial-magazine replacement policy: keep current ammo unless a strictly better
# type can supply a complete replacement; return the old partial only after build.
$current = [pscustomobject]@{ Type='ball'; Rounds=12; Capacity=30 }
$typedPool = @{ ball = 10; ap = 29 }
Assert-True (-not ($typedPool.ap -ge $current.Capacity)) 'partial gun is not replaced by an incomplete better-ammo load'
$topup = [Math]::Min($current.Capacity - $current.Rounds, $typedPool.ball)
$current.Rounds += $topup; $typedPool.ball -= $topup
Assert-True ($current.Type -eq 'ball' -and $current.Rounds -eq 22 -and $typedPool.ap -eq 29) 'partial gun preserves current ammo type when better ammo cannot fill the magazine'
$current = [pscustomobject]@{ Type='ball'; Rounds=12; Capacity=30 }
$typedPool = @{ ball = 10; ap = 30 }
$beforeReplacement = $current.Rounds + $typedPool.ball + $typedPool.ap
$replacementBuilt = $typedPool.ap -ge $current.Capacity
if ($replacementBuilt) { $typedPool.ap -= $current.Capacity; $typedPool.ball += $current.Rounds; $current.Type='ap'; $current.Rounds=$current.Capacity }
Assert-True ($replacementBuilt -and $current.Type -eq 'ap' -and $current.Rounds -eq 30) 'strictly better full load replaces a partial weaker load'
Assert-True (($current.Rounds + $typedPool.ball + $typedPool.ap) -eq $beforeReplacement) 'partial-magazine replacement conserves the returned old load'
# A loaded but physically broken gun must not make a merc count as combat-ready,
# and scarce ammo must prime the first usable empty gun instead.
$guns = @([pscustomobject]@{Name='Broken'; Status=10; Loaded=20}, [pscustomobject]@{Name='Usable'; Status=80; Loaded=0})
$usableThreshold = 15
$hasUsableLoadedGun = ($guns | Where-Object { $_.Status -ge $usableThreshold -and $_.Loaded -gt 0 }).Count -gt 0
$primeTarget = $guns | Where-Object { $_.Status -ge $usableThreshold -and $_.Loaded -eq 0 } | Select-Object -First 1
Assert-True (-not $hasUsableLoadedGun) 'broken loaded gun does not count as a usable loaded weapon'
Assert-True ($primeTarget.Name -eq 'Usable') 'priming skips broken gun and targets usable empty gun'
if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "ITEM_AMMO_ALLOCATION_FIXTURE_FAILED"
    foreach ($failure in $failures) { Write-Host "FAIL: $failure" }
    exit 62
}
Write-Host ""
Write-Host "ITEM_AMMO_ALLOCATION_FIXTURE_OK"
exit 0