class IllegalActionError(ValueError):
    """Raised before any state mutation when an action is not legal."""


class RemoteEnvironmentError(RuntimeError):
    """Raised by ``AsyncMahjongEnv`` when its worker reports an exception."""
