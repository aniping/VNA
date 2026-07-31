# Issue tracker：本地 Markdown

本仓库的 Issue 和规格（PRD）以 Markdown 文件保存在 `.scratch/` 中。

## 约定

- 每项功能使用一个目录：`.scratch/<feature-slug>/`。
- 规格文件为 `.scratch/<feature-slug>/spec.md`。
- 实现工单分别保存为 `.scratch/<feature-slug>/issues/<NN>-<slug>.md`，从 `01` 开始编号；禁止合并成单个 tickets 文件。
- 每个工单靠近文件顶部使用 `Status:` 行记录分类状态，具体值参见 `triage-labels.md`。
- 评论和讨论历史追加到文件末尾的 `## Comments` 标题下。

## 当技能要求“发布到 issue tracker”时

在 `.scratch/<feature-slug>/` 下创建相应 Markdown 文件；目录不存在时一并创建。

## 当技能要求“获取相关工单”时

读取用户给出的工单路径或编号所对应的 Markdown 文件。

## Wayfinder 操作

`wayfinder` 使用一个 map 文件和每个问题各自独立的子工单：

- Map：`.scratch/<effort>/map.md`，保存 Notes、Decisions-so-far 和 Fog。
- 子工单：`.scratch/<effort>/issues/NN-<slug>.md`，正文记录问题。
- 子工单顶部使用 `Type:` 记录 `research`、`prototype`、`grilling` 或 `task`，使用 `Status:` 记录 `claimed` 或 `resolved`。
- 使用 `Blocked by: NN, NN` 记录依赖；列出的所有工单均为 `resolved` 后才解除阻塞。
- Frontier：按编号选择第一个未解决、未阻塞且未认领的工单。
- Claim：先将 `Status:` 改为 `claimed` 并保存，再开始工作。
- Resolve：在 `## Answer` 下追加结论，将 `Status:` 改为 `resolved`，并把结论摘要和链接追加到 map 的 Decisions-so-far。
