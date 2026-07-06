# Mahjong-GPT：基于 GPT 风格模型的立直麻将 Bot

[[English](README.md) | 中文]

Mahjong-GPT 是一个实验性的立直麻将强化学习项目。

## 当前状态与注意事项

- **训练算法**：代码结构围绕 PPO-Penalty 的策略/价值更新组织。
- **规则与计分**：环境包含手牌估值、动作掩码、鸣牌、荣和/自摸、立直、宝牌和奖励塑形等逻辑。正式依赖规则正确性前，建议补充荣和/自摸上下文、食断、叫牌优先级和多家荣和等专项测试。
- **检查点恢复**：当前 episode 级检查点支持实用层面的断点续训，但在 agent 局部 RNG、worker seed、AMP scaler 等运行时状态未全部一致保存和恢复前，不能保证轨迹级精确复现。
- **依赖版本**：`requirements.txt` 目前没有固定版本。若用于可复现实验，建议额外创建 lock 文件或固定依赖版本。

## 环境需求

推荐基线：

- Python 3.14及以上
- 与你的 Python、操作系统、CUDA/CPU 环境兼容的 PyTorch
- `requirements.txt` 中列出的依赖包

默认配置使用 `cuda` 作为设备。如果在仅 CPU 的机器上运行，请先在 pass 配置中把 `system.device` 改为 `cpu`。

## 安装

1. 克隆仓库：

   ```bash
   git clone https://github.com/marko1616/mahjong-gpt.git
   cd mahjong-gpt
   ```

2. 创建并激活虚拟环境：

   ```bash
   python -m venv .venv
   source .venv/bin/activate
   ```

   Windows PowerShell：

   ```powershell
   python -m venv .venv
   .\.venv\Scripts\Activate.ps1
   ```

3. 按你的平台安装 PyTorch，然后安装项目依赖：

   ```bash
   # 根据操作系统和 CUDA/CPU 环境选择合适的 PyTorch 安装命令：
   # https://pytorch.org/get-started/locally/

   pip install -r requirements.txt
   ```

4. 可选开发工具：

   ```bash
   pip install ruff
   ```

## 快速开始

启动交互式训练管理器：

```bash
python cli.py interactive
```

第一次训练通常是：

```text
Choose a task: init-run
Choose a task: run
```

也可以通过环境变量预设 run 根目录和 run id：

```bash
export MAHJONG_GPT_ROOT=/mnt/models/mahjong-gpt
export MAHJONG_GPT_RUN_ID=model-00
python cli.py interactive
```

## CLI 工具

`cli.py` 提供了一个交互式命令行工作流，用于管理多阶段训练实验。

### 核心概念

- **Run（训练运行）**：由 `run_id` 标识的一次完整实验。
- **Pass（训练阶段）**：一个训练阶段，拥有独立配置、检查点、日志和状态。
- **Manifest（清单）**：run 级元数据，记录所有 pass 和当前活跃 pass。
- **Checkpoint（检查点）**：episode 级目录，用于恢复训练或初始化新 pass。

### 可用任务

| 任务               | 说明                                                        |
| ------------------ | ----------------------------------------------------------- |
| `status`           | 显示 run 状态、pass 进度、活跃 pass、检查点路径和最佳指标。 |
| `init-run`         | 初始化新的 run，创建 manifest 和第一个 pass。               |
| `set-active`       | 选择 `TrainerRunner` 要运行的 pass。                        |
| `append-pass`      | 添加新 pass，可选择从其他 pass 的检查点初始化。             |
| `edit-config`      | 在 `$EDITOR` 中编辑 pass 配置，并通过 Pydantic 校验。       |
| `reset-pass-state` | 将 pass 状态重置为 `pending`，不删除检查点。                |
| `run`              | 启动活跃 pass 的训练。                                      |
| `ruff-check`       | 在安装 Ruff 后检查 `src/` 和 `tests/`。                     |
| `ruff-format`      | 在安装 Ruff 后格式化 `src/` 和 `tests/`。                   |
| `pytest-cov`       | 运行 `pytest --cov=src`。                                   |
| `exit`             | 退出 CLI。                                                  |

### 示例：先训练，再用新超参继续训练

```text
# 1. 初始化 run 和 pass-0
Choose a task: init-run
Run notes: First experiment
First pass name: pass-0
total_episodes for pass-0: 1000
→ Initialized run.

# 2. 启动训练
Choose a task: run
Run active pass 0 now? Yes
→ TrainerRunner begins pass-0

# 3. pass-0 完成后追加 pass-1
Choose a task: append-pass
Config source: Edit JSON in $EDITOR
New pass name: pass-1-finetune
Bootstrap from existing pass checkpoint? Yes
Select source pass: 0 - pass-0 (completed)
Source episode (blank = latest): [Enter]
init_mode: weights_only
→ Appended pass 1 and set active.

# 4. 继续训练
Choose a task: run
→ TrainerRunner loads pass-0 weights and executes pass-1
```

## 配置

配置定义在 `src/config.py` 中，并使用 Pydantic 模型管理。

主要配置区块：

| 区块             | 作用                                                                                  |
| ---------------- | ------------------------------------------------------------------------------------- |
| `training`       | Replay buffer 大小、batch/update 参数、学习率、epsilon scheduler 和 pass episode 数。 |
| `env`            | 环境 seed 和环境数量。                                                                |
| `target`         | return/target 计算参数和 scheduler。                                                  |
| `model`          | GPT 风格模型维度和 token 词表设置。                                                   |
| `system`         | 设备、dtype、AMP、worker 数量和输出文件名。                                           |
| `reward`         | 奖励塑形权重和惩罚项。                                                                |
| `eval` / `evalu` | 评估模式行为。                                                                        |

编辑配置时，优先使用 CLI 的 `edit-config` 任务，这样保存前会自动做校验。

## 项目结构

```text
cli.py                  # 交互式 CLI 工具
src/
├── agent.py            # 策略/价值 agent 与异步环境编排
├── ckpt_manager.py     # run/pass/checkpoint 文件系统管理
├── config.py           # Pydantic 配置模型
├── model.py            # GPT 风格模型定义
├── recorder.py         # 指标记录与日志
├── schedulers.py       # Scheduler 实现
├── schemes.py          # Trail、ReplayBuffer 等数据结构
├── trainer.py          # 高层训练 runner
├── env/
│   ├── constants.py    # 动作和 token id 常量
│   ├── env.py          # 麻将环境主体
│   ├── event_bus.py    # 事件系统
│   ├── hand.py         # 手牌表示与手牌估值集成
│   ├── player.py       # 玩家状态
│   ├── tiles.py        # 牌面转换工具
│   ├── tokens.py       # TokenList 与词表辅助工具
│   ├── wall.py         # 牌山生成与发牌
│   └── worker.py       # 异步多进程环境封装
└── utils/
    ├── ckpt_utils.py   # 检查点工具与 RNG 辅助函数
    ├── rl_utils.py     # 强化学习数学工具
    └── stats_utils.py  # 统计工具

tests/
├── test_agent.py
└── utils/
```

## 动作与 Token ID

环境每个座位使用 **46 个本地动作**，索引为 `0` 到 `45`。

| 动作 ID  | 名称 | 说明                                 |
| -------- | ---- | ------------------------------------ |
| `0`-`33` | 打牌 | 按 `tile34 = action_id` 打出对应牌。 |
| `34`     | 上吃 | 以被打出的牌作为顺子的上端。         |
| `35`     | 中吃 | 以被打出的牌作为顺子的中间牌。       |
| `36`     | 下吃 | 以被打出的牌作为顺子的下端。         |
| `37`     | 碰   | 碰牌。                               |
| `38`     | 明杠 | 大明杠 / open kan。                  |
| `39`     | 加杠 | 小明杠 / added kan。                 |
| `40`     | 暗杠 | 暗杠 / closed kan。                  |
| `41`     | 拔北 | 三人麻将拔北预留。                   |
| `42`     | 立直 | 宣告立直。                           |
| `43`     | 荣和 | 对他家舍牌和牌。                     |
| `44`     | 自摸 | 自摸和牌。                           |
| `45`     | 跳过 | 放弃动作或鸣牌。                     |

序列模型使用的 token id 包括：

| Token ID 范围 | 含义             |
| ------------- | ---------------- |
| `0`-`45`      | 动作 token。     |
| `46`-`79`     | 手牌牌面 token。 |
| `80`-`83`     | 玩家 token。     |
| `84`          | 分隔 token。     |
| `85`          | 填充 token。     |

## 检查点与断点续训

训练 runner 会在活跃 run 目录下写入 episode 级检查点。当前检查点包含：

- policy 和 value 模型权重；
- optimizer 状态；
- 全局 RNG 状态；
- replay buffer 内容；
- scheduler 状态；
- episode 指标元数据。

支持的工作流：

- 从当前 pass 的最新检查点恢复；
- 使用 `weights_only` 或 `full` 模式从旧 pass 初始化新 pass；
- 使用 manifest 管理多阶段实验。

当前支持实用层面的断点续训。若要做到轨迹级精确复现，还需要保证所有运行时状态都被一致保存和恢复。

## 测试

运行单元测试：

```bash
pytest
```

运行覆盖率测试：

```bash
pytest --cov=src
```

## License

Apache-2.0
