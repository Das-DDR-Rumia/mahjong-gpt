# Mahjong-GPT: Riichi Mahjong Bot Based on a GPT-Style Model

[English | [中文](README_zh.md)]

Mahjong-GPT is an experimental Riichi Mahjong reinforcement-learning project.

## Current Status and Caveats

- **Training algorithm**: the codebase is organized around a PPO-Penalty policy/value update.
- **Rules and scoring**: the environment integrates hand calculation, action masks, calls, Ron/Tsumo, riichi, dora, and reward shaping. Add focused tests for Ron/Tsumo context, open tanyao, claim priority, and multi-ron behavior before relying on rule-level correctness.
- **Checkpoint resume**: episode checkpoints support practical resume, but trajectory-level exact resume is not guaranteed in every configuration until all runtime state, including agent-local RNG, worker seeds, and AMP scaler state, is saved and restored consistently.
- **Dependency versions**: `requirements.txt` currently lists dependencies without version pins. For reproducible experiments, create a lock file or pin package versions in your environment.

## Requirements

Recommended baseline:

- Python 3.14 or above
- PyTorch compatible with your Python version, operating system, and CUDA/CPU setup
- Packages listed in `requirements.txt`

The default configuration uses `cuda` as the device. On a CPU-only machine, change `system.device` to `cpu` in the pass configuration before running training.

## Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/marko1616/mahjong-gpt.git
   cd mahjong-gpt
   ```

2. Create and activate a virtual environment:

   ```bash
   python -m venv .venv
   source .venv/bin/activate
   ```

   On Windows PowerShell:

   ```powershell
   python -m venv .venv
   .\.venv\Scripts\Activate.ps1
   ```

3. Install PyTorch for your platform, then install the project dependencies:

   ```bash
   # Choose the PyTorch command that matches your OS/CUDA setup:
   # https://pytorch.org/get-started/locally/

   pip install -r requirements.txt
   ```

4. Optional developer tools:

   ```bash
   pip install ruff
   ```

## Quick Start

Run the interactive training manager:

```bash
python cli.py interactive
```

A typical first run is:

```text
Choose a task: init-run
Choose a task: run
```

You can also set the run root and run id with environment variables:

```bash
export MAHJONG_GPT_ROOT=/mnt/models/mahjong-gpt
export MAHJONG_GPT_RUN_ID=model-00
python cli.py interactive
```

## CLI Tool

`cli.py` provides an interactive command-line workflow for multi-pass experiments.

### Core Concepts

- **Run**: a complete experiment identified by `run_id`.
- **Pass**: one training phase with its own config, checkpoints, logs, and state.
- **Manifest**: run-level metadata that tracks all passes and the active pass.
- **Checkpoint**: an episode-level directory containing model and training state used for resume or bootstrapping.

### Available Tasks

| Task               | Description                                                                        |
| ------------------ | ---------------------------------------------------------------------------------- |
| `status`           | Show run status, pass progress, active pass, checkpoint pointers, and best metric. |
| `init-run`         | Initialize a new run with a manifest and first pass.                               |
| `set-active`       | Select which pass `TrainerRunner` should run.                                      |
| `append-pass`      | Add a new pass, optionally initialized from another pass checkpoint.               |
| `edit-config`      | Edit a pass config in `$EDITOR` with Pydantic validation.                          |
| `reset-pass-state` | Reset a pass state to `pending` without deleting checkpoints.                      |
| `run`              | Launch training on the active pass.                                                |
| `ruff-check`       | Run Ruff check on `src/` and `tests/` if Ruff is installed.                        |
| `ruff-format`      | Run Ruff format on `src/` and `tests/` if Ruff is installed.                       |
| `pytest-cov`       | Run `pytest --cov=src`.                                                            |
| `exit`             | Exit the CLI.                                                                      |

### Example: Train, Then Continue With New Hyperparameters

```text
# 1. Initialize run and pass-0
Choose a task: init-run
Run notes: First experiment
First pass name: pass-0
total_episodes for pass-0: 1000
→ Initialized run.

# 2. Start training
Choose a task: run
Run active pass 0 now? Yes
→ TrainerRunner begins pass-0

# 3. After pass-0 completes, append pass-1
Choose a task: append-pass
Config source: Edit JSON in $EDITOR
New pass name: pass-1-finetune
Bootstrap from existing pass checkpoint? Yes
Select source pass: 0 - pass-0 (completed)
Source episode (blank = latest): [Enter]
init_mode: weights_only
→ Appended pass 1 and set active.

# 4. Continue training
Choose a task: run
→ TrainerRunner loads pass-0 weights and executes pass-1
```

## Configuration

Configuration lives in `src/config.py` and is built with Pydantic models.

Main sections:

| Section          | Purpose                                                                                               |
| ---------------- | ----------------------------------------------------------------------------------------------------- |
| `training`       | Replay buffer size, batch/update settings, learning rates, epsilon scheduler, and pass episode count. |
| `env`            | Environment seed and number of environments.                                                          |
| `target`         | Return/target calculation settings and schedulers.                                                    |
| `model`          | GPT-style model dimensions and token vocabulary settings.                                             |
| `system`         | Device, dtype, AMP options, worker count, and output filenames.                                       |
| `reward`         | Reward shaping weights and penalties.                                                                 |
| `eval` / `evalu` | Evaluation-mode behavior.                                                                             |

When editing configs, prefer the CLI `edit-config` task so the result is validated before being saved.

## Project Structure

```text
cli.py                  # Interactive CLI tool
src/
├── agent.py            # Policy/value agent and async environment orchestration
├── ckpt_manager.py     # Run/pass/checkpoint filesystem management
├── config.py           # Pydantic configuration models
├── model.py            # GPT-style model definition
├── recorder.py         # Metrics recording and logging
├── schedulers.py       # Scheduler implementations
├── schemes.py          # Data structures such as Trail and ReplayBuffer
├── trainer.py          # High-level training runner
├── env/
│   ├── constants.py    # Action and token id constants
│   ├── env.py          # Main Mahjong environment
│   ├── event_bus.py    # Event system
│   ├── hand.py         # Hand representation and hand-value integration
│   ├── player.py       # Player state
│   ├── tiles.py        # Tile conversion utilities
│   ├── tokens.py       # TokenList and vocabulary helpers
│   ├── wall.py         # Wall generation and dealing
│   └── worker.py       # Async multiprocessing environment wrapper
└── utils/
    ├── ckpt_utils.py   # Checkpoint utilities and RNG helpers
    ├── rl_utils.py     # RL math utilities
    └── stats_utils.py  # Statistics helpers

tests/
├── test_agent.py
└── utils/
```

## Action and Token IDs

The environment uses **46 local actions per seat**, indexed from `0` to `45`.

| Action ID | Name       | Description                                     |
| --------- | ---------- | ----------------------------------------------- |
| `0`-`33`  | Discard    | Discard tile by `tile34 = action_id`.           |
| `34`      | Chi up     | Chi with the discarded tile as the upper tile.  |
| `35`      | Chi middle | Chi with the discarded tile as the middle tile. |
| `36`      | Chi down   | Chi with the discarded tile as the lower tile.  |
| `37`      | Pon        | Pon call.                                       |
| `38`      | Open kan   | Daiminkan / open kan.                           |
| `39`      | Added kan  | Shouminkan / added kan.                         |
| `40`      | Closed kan | Ankan / closed kan.                             |
| `41`      | Pei        | Reserved for 3-player Mahjong north extraction. |
| `42`      | Riichi     | Declare riichi.                                 |
| `43`      | Ron        | Win on another player's discard.                |
| `44`      | Tsumo      | Win by self-draw.                               |
| `45`      | Pass       | Decline action or call.                         |

Token ids used by the sequence model include:

| Token ID Range | Meaning           |
| -------------- | ----------------- |
| `0`-`45`       | Action tokens.    |
| `46`-`79`      | Hand tile tokens. |
| `80`-`83`      | Player tokens.    |
| `84`           | Separator token.  |
| `85`           | Padding token.    |

## Checkpointing and Resume

The training runner writes episode checkpoints under the active run directory. A checkpoint currently includes:

- policy and value model weights;
- optimizer states;
- global RNG state;
- replay buffer contents;
- scheduler states;
- episode metrics metadata.

Supported workflows:

- resume a pass from its latest checkpoint;
- initialize a new pass from an earlier pass with `weights_only` or `full` mode;
- keep pass metadata in a manifest for multi-stage experiments.

Practical resume is supported. Exact trajectory-level reproducibility still requires saving and restoring every runtime state consistently.

## Testing

Run unit tests:

```bash
pytest
```

Run tests with coverage:

```bash
pytest --cov=src
```

## License

Apache-2.0
