# 第三方依赖

本目录保存构建所需的固定版本第三方依赖。产品代码不得直接依赖测试专用库。

## GoogleTest

- 版本：1.17.0
- 上游地址：`https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip`
- 本地归档：`packages/googletest-v1.17.0.zip`
- SHA256：`40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff`
- 用途：仅在 `BUILD_TESTING=ON` 时构建和链接测试程序。

更新依赖时必须同时更新版本、归档、SHA256 和本文件中的来源记录，并完成全新构建目录验证。
