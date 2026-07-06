import asyncio
import multiprocessing as mp
import traceback
from dataclasses import dataclass
from typing import Any, Dict, Optional, Tuple

try:
    from ..config import RewardConfig  # type: ignore
except ImportError:
    from .config import RewardConfig
from .env import MahjongEnv
from .errors import RemoteEnvironmentError

CMD_RESET = 0
CMD_STEP = 1
CMD_CLOSE = 2


@dataclass(frozen=True)
class _WorkerError:
    error_type: str
    message: str
    traceback_text: str


def _worker_loop(pipe, seed: int, reward_config: RewardConfig) -> None:
    env = MahjongEnv(seed=seed, rc=reward_config)
    try:
        while True:
            command, data = pipe.recv()
            if command == CMD_CLOSE:
                pipe.send((True, None))
                return
            try:
                if command == CMD_RESET:
                    result = env.reset(seed=data)
                elif command == CMD_STEP:
                    result = env.step(data)
                else:
                    raise ValueError(f"unknown worker command: {command}")
                pipe.send((True, result))
            except BaseException as exc:
                pipe.send(
                    (
                        False,
                        _WorkerError(
                            error_type=type(exc).__name__,
                            message=str(exc),
                            traceback_text=traceback.format_exc(),
                        ),
                    )
                )
    except EOFError:
        return
    finally:
        pipe.close()


class AsyncMahjongEnv:
    """Awaitable proxy with serialized pipe access and propagated worker errors."""

    def __init__(self, seed: int, reward_config: RewardConfig) -> None:
        self.ctx = mp.get_context("spawn")
        self.parent_conn, child_conn = self.ctx.Pipe()
        self.process = self.ctx.Process(
            target=_worker_loop,
            args=(child_conn, seed, reward_config),
        )
        self.process.start()
        child_conn.close()
        self._lock = asyncio.Lock()
        self._closed = False

    async def reset(self, seed: Optional[int] = None) -> Tuple[Dict, float, bool, Dict]:
        return await self._request(CMD_RESET, seed)

    async def step(self, action: int) -> Tuple[Dict, float, bool, Dict]:
        return await self._request(CMD_STEP, action)

    async def _request(self, command: int, data: Any):
        async with self._lock:
            if self._closed:
                raise RuntimeError("AsyncMahjongEnv is closed")
            loop = asyncio.get_running_loop()
            return await loop.run_in_executor(
                None, self._send_and_receive, command, data
            )

    def _send_and_receive(self, command: int, data: Any):
        self.parent_conn.send((command, data))
        ok, payload = self.parent_conn.recv()
        if ok:
            return payload
        assert isinstance(payload, _WorkerError)
        raise RemoteEnvironmentError(
            f"worker {payload.error_type}: {payload.message}\n{payload.traceback_text}"
        )

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            if self.process.is_alive():
                self.parent_conn.send((CMD_CLOSE, None))
                self.parent_conn.recv()
                self.process.join(timeout=2.0)
            if self.process.is_alive():
                self.process.terminate()
                self.process.join(timeout=2.0)
        except (BrokenPipeError, EOFError, OSError):
            pass
        finally:
            try:
                self.parent_conn.close()
            except OSError:
                pass

    def __enter__(self) -> "AsyncMahjongEnv":
        return self

    def __exit__(self, exc_type, exc, traceback_obj) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass
