# Linux/ARM64 QEMU 验证

本文记录项目固定的 Linux/ARM64 补充验证环境和可重复执行脚本。该环境运行在
x86-64 Windows 宿主上的 QEMU/binfmt 模拟层，只验证编译、链接和功能正确性，
不能代表原生 ARM64 性能，也不扩大[平台支持矩阵](support-matrix.md)中的正式承诺。

## 固定环境

| 项目 | 固定值 |
| --- | --- |
| WSL 发行版 | `Ubuntu` |
| Docker 容器 | `arm64-ssh`，镜像 `arm64-ssh:ubuntu24.04` |
| 容器架构 | `linux/arm64`；`uname -m` 必须输出 `aarch64` |
| SSH | `127.0.0.1:2222`，用户 `arm64` |
| 私钥路径 | `%USERPROFILE%\.ssh\arm64-docker-ed25519` |
| 编译器 | GCC/G++ 13.3.0 |
| 构建工具 | CMake 3.28.3、GNU Make 4.3、Git 2.43.0 |
| 基础工具包 | `build-essential` |

Git 是构建依赖：CMake 使用它把仓库内的补丁应用到构建目录中的 cpp-httplib
副本。脚本只把私钥路径交给 `ssh` 和 `scp`，不会读取或复制私钥内容。

## 执行前提

- 在 Windows PowerShell 中、仓库根目录执行。
- 当前分支必须是 `main`，`HEAD` 等于本地 `main`，工作树必须 clean。
- `third-part/` Git submodule 已初始化，并与主仓库记录的 gitlink 一致。
- 端口 `2222` 未被其他服务占用。
- 脚本只归档已提交对象，不同步 `.git`、`build/`、`out/`、`release/`、
  `node_modules/` 或其他工作副本文件。

## 完整验证脚本

```powershell
$ErrorActionPreference = 'Stop'

$repository = (Resolve-Path '.').Path
$keyPath = Join-Path $env:USERPROFILE '.ssh\arm64-docker-ed25519'
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ('vna-arm64-stage-' + [Guid]::NewGuid().ToString('N'))
$sourceStage = Join-Path $stageRoot 'source'
$bundle = Join-Path $stageRoot 'vna-source.tar'
$remoteSource = $null
$remotePattern = '^/tmp/vna-arm64\.[A-Za-z0-9]{6}$'
$keeper = $null
$containerStarted = $false

function Invoke-Arm64Ssh {
  param([Parameter(Mandatory)][string]$Command)
  & ssh.exe -i $keyPath -p 2222 -o BatchMode=yes `
    -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new `
    arm64@127.0.0.1 $Command
  if ($LASTEXITCODE -ne 0) {
    throw "ARM64 SSH command failed with exit code $LASTEXITCODE"
  }
}

function Invoke-TimedArm64Ssh {
  param(
    [Parameter(Mandatory)][string]$Label,
    [Parameter(Mandatory)][string]$Command
  )
  $timer = [Diagnostics.Stopwatch]::StartNew()
  Invoke-Arm64Ssh $Command
  $timer.Stop()
  Write-Host ("{0}_WALL_SECONDS={1:N3}" -f $Label, $timer.Elapsed.TotalSeconds)
}

try {
  if (-not (Test-Path -LiteralPath $keyPath -PathType Leaf)) {
    throw "SSH private key not found: $keyPath"
  }
  foreach ($tool in @('git', 'tar.exe', 'ssh.exe', 'scp.exe', 'wsl.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
      throw "Required host tool not found: $tool"
    }
  }
  if ((git status --porcelain)) {
    throw 'The repository must be clean before ARM64 validation.'
  }
  if ((git branch --show-current) -ne 'main') {
    throw 'ARM64 validation must archive the current main branch.'
  }
  if ((git rev-parse HEAD) -ne (git rev-parse main)) {
    throw 'HEAD does not match the local main ref.'
  }
  $submoduleStatus = @(git submodule status --recursive)
  if ($LASTEXITCODE -ne 0 -or
      ($submoduleStatus | Where-Object { $_[0] -ne ' ' })) {
    throw 'A submodule is missing or does not match the recorded gitlink.'
  }

  # WSL 没有活跃客户端时可能退出，并连带停止其 Docker daemon。保活进程只覆盖
  # 本次验证，使分开的 SSH、SCP、构建和测试命令共享同一容器生命周期。
  $keeper = Start-Process -FilePath 'wsl.exe' `
    -ArgumentList @('-d', 'Ubuntu', '--', 'sleep', '7200') `
    -WindowStyle Hidden -PassThru
  Start-Sleep -Seconds 2
  $runningContainer = wsl.exe -d Ubuntu -- docker ps --quiet `
    --filter 'name=^/arm64-ssh$' --filter 'status=running'
  if ($LASTEXITCODE -ne 0) { throw 'Failed to inspect arm64-ssh.' }
  if (-not $runningContainer) {
    wsl.exe -d Ubuntu -- docker start arm64-ssh
    if ($LASTEXITCODE -ne 0) { throw 'Failed to start arm64-ssh.' }
    $containerStarted = $true
  }

  $sshReady = $false
  for ($attempt = 1; $attempt -le 20; $attempt++) {
    $probe = Test-NetConnection 127.0.0.1 -Port 2222 `
      -WarningAction SilentlyContinue
    if ($probe.TcpTestSucceeded) { $sshReady = $true; break }
    Start-Sleep -Seconds 1
  }
  if (-not $sshReady) { throw 'SSH port 2222 did not become ready.' }

  Invoke-Arm64Ssh @'
set -eu
test "$(uname -m)" = aarch64
test "$(git --version)" = "git version 2.43.0"
test "$(gcc -dumpfullversion)" = "13.3.0"
test "$(g++ -dumpfullversion)" = "13.3.0"
test "$(cmake --version | head -1)" = "cmake version 3.28.3"
test "$(make --version | head -1)" = "GNU Make 4.3"
command -v readelf >/dev/null
dpkg-query -W build-essential
uname -m
gcc --version | head -1
g++ --version | head -1
cmake --version | head -1
make --version | head -1
git --version
'@

  New-Item -ItemType Directory -Path $sourceStage | Out-Null
  $rootArchive = Join-Path $stageRoot 'root.tar'
  git archive --format=tar -o $rootArchive HEAD
  if ($LASTEXITCODE -ne 0) { throw 'Failed to archive the main repository.' }
  tar.exe -xf $rootArchive -C $sourceStage
  if ($LASTEXITCODE -ne 0) { throw 'Failed to extract the main repository.' }

  $moduleLines = git config --file .gitmodules --get-regexp path
  if ($LASTEXITCODE -ne 0) { throw 'Failed to enumerate submodules.' }
  foreach ($line in $moduleLines) {
    $module = ($line -split '\s+', 2)[1]
    $moduleSource = Join-Path $repository ($module -replace '/', '\')
    $moduleTarget = Join-Path $sourceStage ($module -replace '/', '\')
    $moduleArchive = Join-Path $stageRoot (($module -replace '/', '-') + '.tar')
    New-Item -ItemType Directory -Path $moduleTarget -Force | Out-Null
    git -C $moduleSource archive --format=tar -o $moduleArchive HEAD
    if ($LASTEXITCODE -ne 0) { throw "Failed to archive $module." }
    tar.exe -xf $moduleArchive -C $moduleTarget
    if ($LASTEXITCODE -ne 0) { throw "Failed to extract $module." }
  }
  tar.exe -cf $bundle -C $sourceStage .
  if ($LASTEXITCODE -ne 0) { throw 'Failed to create the source bundle.' }

  $remoteSource = (& ssh.exe -i $keyPath -p 2222 -o BatchMode=yes `
    -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new `
    arm64@127.0.0.1 'mktemp -d /tmp/vna-arm64.XXXXXX').Trim()
  if ($LASTEXITCODE -ne 0 -or $remoteSource -notmatch $remotePattern) {
    throw 'Failed to create a bounded remote validation directory.'
  }
  $remoteBundle = "$remoteSource/vna-source.tar"
  & scp.exe -i $keyPath -P 2222 -o BatchMode=yes $bundle `
    ("arm64@127.0.0.1:" + $remoteBundle)
  if ($LASTEXITCODE -ne 0) { throw 'Failed to transfer the source bundle.' }
  Invoke-Arm64Ssh `
    "tar -xf '$remoteBundle' -C '$remoteSource' && rm '$remoteBundle'"
  Invoke-Arm64Ssh `
    ("test ! -e '$remoteSource/.git' && test ! -e '$remoteSource/build' && " +
     "test ! -e '$remoteSource/out' && test ! -e '$remoteSource/release' && " +
     "test ! -e '$remoteSource/node_modules'")

  Invoke-TimedArm64Ssh 'CONFIGURE' `
    ("cd '$remoteSource' && cmake -S . -B build-arm64 " +
     "-G 'Unix Makefiles' -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug " +
     '-DCMAKE_CXX_COMPILER=g++')
  Invoke-TimedArm64Ssh 'FOCUSED_BUILD' `
    ("cd '$remoteSource' && cmake --build build-arm64 " +
     '--target vna_logging_tests vna-server -- -j2')
  Invoke-TimedArm64Ssh 'LOGGING_TESTS' `
    ("cd '$remoteSource' && ctest --test-dir build-arm64 " +
     "-R 'JsonLinesLogger' --no-tests=error --output-on-failure -j1")

  Invoke-Arm64Ssh @"
set -eu
cd '$remoteSource'
test -f build-arm64/third-part/spdlog/libspdlogd.a
assert_aarch64_elf() {
  header="`$(readelf -h "`$1")"
  printf '%s\n' "`$header"
  printf '%s\n' "`$header" | grep -Eq 'Class:[[:space:]]+ELF64'
  printf '%s\n' "`$header" | grep -Eq "Data:[[:space:]]+2's complement, little endian"
  printf '%s\n' "`$header" | grep -Eq 'Machine:[[:space:]]+AArch64'
}
assert_aarch64_elf build-arm64/third-part/spdlog/CMakeFiles/spdlog.dir/src/spdlog.cpp.o
assert_aarch64_elf build-arm64/apps/vna-server/vna-server
assert_aarch64_elf build-arm64/tests/integration/vna_logging_tests
"@

  Invoke-TimedArm64Ssh 'FULL_BUILD' `
    "cd '$remoteSource' && cmake --build build-arm64 -- -j2"
  Invoke-TimedArm64Ssh 'FULL_CTEST' `
    ("cd '$remoteSource' && ctest --test-dir build-arm64 " +
     '--no-tests=error --output-on-failure -j1')
}
finally {
  if ($remoteSource -and $remoteSource -match $remotePattern) {
    $safeRemoteCleanup =
      "case '$remoteSource' in /tmp/vna-arm64." +
      '[A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9][A-Za-z0-9]) ' +
      "rm -rf -- '$remoteSource' ;; *) exit 2 ;; esac"
    & ssh.exe -i $keyPath -p 2222 -o BatchMode=yes arm64@127.0.0.1 `
      $safeRemoteCleanup 2>$null | Out-Null
  }
  if ($containerStarted) {
    wsl.exe -d Ubuntu -- docker stop arm64-ssh 2>$null | Out-Null
  }
  if ($keeper -and -not $keeper.HasExited) {
    Stop-Process -Id $keeper.Id -ErrorAction SilentlyContinue
  }

  $resolvedStage = [IO.Path]::GetFullPath($stageRoot)
  $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
  if ([IO.Path]::GetDirectoryName($resolvedStage).TrimEnd('\') -eq $tempRoot -and
      [IO.Path]::GetFileName($resolvedStage) -like 'vna-arm64-stage-*') {
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
  }
}
```

## 结果判定

通过必须同时满足：环境报告 `aarch64`；根配置成功；聚焦日志测试和完整 CTest
均无失败；spdlog 为静态库；`readelf` 对 spdlog 对象、`vna-server` 和
`vna_logging_tests` 都报告 `ELF64`、little-endian、`AArch64`。报告分别记录
配置、聚焦构建、聚焦测试、完整构建和完整 CTest 的 wall time。

若预检缺少 Git、编译器或 `readelf`，应停止并报告缺失工具，不得把未执行目标
描述为通过。QEMU 下的低速属于预期现象；不要用这些耗时推断真实 ARM64 设备性能。
