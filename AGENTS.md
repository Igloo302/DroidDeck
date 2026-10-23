# Rules

- Comments should be rare, factual, and explain only non-obvious external constraints or business invariants.

## 项目文件与上下文管理

**让 Codex 访问项目文件，按任务定位和读取。** 仓库可访问，不等于全部代码都进入了每一轮上下文；无需每次粘贴或要求通读整个项目。

1. **明确这次改什么。** 提供目标、相关目录或文件、报错和验收标准，减少盲目搜索。
2. **先定位，再阅读。** 先查相关入口和 `git diff`，再沿函数、接口与调用链读取必要代码；信息不足再扩大范围。保留对依赖和影响范围的检查。
3. **把项目说明写短。** `AGENTS.md` 保留关键约束、构建和测试命令；模块索引与详细架构单独保存，按需读取。项目指令会进入上下文，堆得越多，常驻内容越多。[指令机制](https://learn.chatgpt.com/docs/agent-configuration/agents-md)
4. **用简短记录承接下一阶段。** 可维护 `TASK.md`，记录目标、分支或版本、关键决定、已改文件、测试和待办。换阶段或聊天混入太多无关内容时，让新聊天读取它并核对当前代码；同一小任务继续原聊天即可。[阶段记录参考](https://developers.openai.com/cookbook/examples/codex/iterating-development-workflows-with-codex)
5. **控制工具输出。** 日志先看报错附近，搜索排除无关依赖与构建产物；改后给摘要和必要差异，相关测试照常执行。
