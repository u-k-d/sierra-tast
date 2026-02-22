param(
  [string]$Compiler = "clang++",
  [switch]$EnableAcsil = $true
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$sources = @(
  "src/core/runtime.cpp",
  "src/plugin/sre_plugin.cpp",
  "src/cli/sre_planlint.cpp",
  "src/layers/L0_core_identity_io/layer.cpp",
  "src/layers/L1_universe_data/layer.cpp",
  "src/layers/L2_sierra_runtime_topology/layer.cpp",
  "src/layers/L3_studies_features/layer.cpp",
  "src/layers/L4_rule_chain_dsl/layer.cpp",
  "src/layers/L5_experimentation_permute/layer.cpp",
  "src/layers/L6_semantics_integrity/layer.cpp",
  "src/layers/L7_outputs_repro/layer.cpp",
  "src/layers/L8_governance_evolution/layer.cpp"
)

$common = @(
  "-std=c++20",
  "-fsyntax-only",
  "-I.",
  "-Iinclude",
  "-Igenerated",
  "-Igenerated/contracts",
  "-IACS_Source"
)

if ($EnableAcsil) {
  $common += "-DSRE_ENABLE_ACSIL=1"
}

foreach ($src in $sources) {
  Write-Host "AST check: $src"
  & $Compiler @common $src
  if ($LASTEXITCODE -ne 0) {
    throw "AST check failed for $src"
  }
}

Write-Host "clang++ AST checks passed."
