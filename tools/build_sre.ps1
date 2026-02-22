param(
  [string]$Compiler = "g++",
  [string]$Config = "release",
  [switch]$EnableAcsil = $true
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$cxxFlags = @(
  "-std=c++20",
  "-I.",
  "-Iinclude",
  "-Igenerated",
  "-Igenerated/contracts",
  "-IACS_Source"
)

if ($Config -eq "debug") {
  $cxxFlags += "-O0"
  $cxxFlags += "-g"
} else {
  $cxxFlags += "-O2"
}

if ($EnableAcsil) {
  $cxxFlags += "-DSRE_ENABLE_ACSIL=1"
}

$sources = @(
  "src/core/runtime.cpp",
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

$dll = Join-Path $buildDir "sre_runtime.dll"
$cli = Join-Path $buildDir "sre_planlint.exe"

$pluginArgs = @()
$pluginArgs += $cxxFlags
$pluginArgs += @("-shared", "-o", $dll, "src/plugin/sre_plugin.cpp")
$pluginArgs += $sources

$cliArgs = @()
$cliArgs += $cxxFlags
$cliArgs += @("-o", $cli, "src/cli/sre_planlint.cpp")
$cliArgs += $sources

Write-Host "Building DLL: $dll"
& $Compiler @pluginArgs

Write-Host "Building CLI: $cli"
& $Compiler @cliArgs

Write-Host "Build complete."

