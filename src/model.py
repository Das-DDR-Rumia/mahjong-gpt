import math
from typing import Optional

import torch
import torch.nn as nn
import torch.nn.functional as F


class RotaryEmbedding(nn.Module):
    """Build RoPE cosine and sine tensors for attention heads."""

    def __init__(self, dim: int, base: float = 10000.0) -> None:
        super().__init__()
        if dim % 2 != 0:
            raise ValueError("RoPE requires head_dim to be even.")

        inv_freq = 1.0 / (base ** (torch.arange(0, dim, 2).float() / dim))
        self.register_buffer("inv_freq", inv_freq, persistent=False)

    def forward(
        self,
        L: int,
        device: torch.device,
        dtype: torch.dtype,
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """Return RoPE tensors with shape [1, 1, L, D/2]."""
        positions = torch.arange(L, device=device, dtype=self.inv_freq.dtype)
        freqs = torch.outer(positions, self.inv_freq.to(device))

        cos = freqs.cos().to(dtype)[None, None, :, :]
        sin = freqs.sin().to(dtype)[None, None, :, :]
        return cos, sin


def apply_rope(
    x: torch.Tensor,
    cos: torch.Tensor,
    sin: torch.Tensor,
) -> torch.Tensor:
    x_even = x[..., ::2]
    x_odd = x[..., 1::2]

    x_rotated = torch.stack(
        (
            x_even * cos - x_odd * sin,
            x_even * sin + x_odd * cos,
        ),
        dim=-1,
    )

    return x_rotated.flatten(-2)


class CausalSelfAttention(nn.Module):
    """Multi-head self-attention with RoPE."""

    def __init__(
        self,
        d_embed: int,
        nhead: int,
        dropout: float,
        causal: bool = True,
    ) -> None:
        super().__init__()
        if d_embed % nhead != 0:
            raise ValueError("d_embed must be divisible by nhead.")

        self.nhead = nhead
        self.head_dim = d_embed // nhead
        self.causal = causal

        self.qkv = nn.Linear(d_embed, 3 * d_embed, bias=False)
        self.out_proj = nn.Linear(d_embed, d_embed, bias=False)
        self.attn_dropout = nn.Dropout(dropout)
        self.resid_dropout = nn.Dropout(dropout)
        self.rope = RotaryEmbedding(self.head_dim)

    def forward(
        self,
        x: torch.Tensor,
        attention_mask: Optional[torch.Tensor] = None,
    ) -> torch.Tensor:
        """Apply self-attention to x with optional padding mask."""
        B, L, C = x.shape

        qkv = self.qkv(x)  # [B, L, 3C]
        qkv = qkv.view(B, L, 3, self.nhead, self.head_dim)
        qkv = qkv.permute(2, 0, 3, 1, 4)  # [3, B, H, L, D]
        q, k, v = qkv.unbind(dim=0)  # [B, H, L, D]

        cos, sin = self.rope(L, x.device, x.dtype)
        q = apply_rope(q, cos, sin)  # [B, H, L, D]
        k = apply_rope(k, cos, sin)  # [B, H, L, D]

        scores = q @ k.transpose(-2, -1)  # [B, H, L, L]
        scores = scores / math.sqrt(self.head_dim)

        mask_value = torch.finfo(scores.dtype).min

        if self.causal:
            causal_mask = torch.ones(L, L, device=x.device, dtype=torch.bool).tril()
            scores = scores.masked_fill(~causal_mask[None, None, :, :], mask_value)

        if attention_mask is not None:
            key_mask = attention_mask[:, None, None, :].bool()
            scores = scores.masked_fill(~key_mask, mask_value)

        attn = F.softmax(scores, dim=-1)  # [B, H, L, L]
        attn = self.attn_dropout(attn)

        y = attn @ v  # [B, H, L, D]
        y = y.transpose(1, 2).contiguous().view(B, L, C)  # [B, L, C]
        y = self.out_proj(y)  # [B, L, C]
        y = self.resid_dropout(y)  # [B, L, C]

        return y


class FeedForward(nn.Module):
    """Position-wise feed-forward network."""

    def __init__(
        self,
        d_embed: int,
        d_ffn: int,
        dropout: float,
    ) -> None:
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(d_embed, d_ffn),
            nn.GELU(),
            nn.Linear(d_ffn, d_embed),
            nn.Dropout(dropout),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Apply feed-forward transformation."""
        return self.net(x)


class GPTBlock(nn.Module):
    """Pre-norm GPT block with RoPE attention and GELU MLP."""

    def __init__(
        self,
        d_embed: int,
        d_ffn: int,
        dropout: float,
        nhead: int,
        causal: bool = True,
    ) -> None:
        super().__init__()
        self.ln_1 = nn.LayerNorm(d_embed)
        self.attn = CausalSelfAttention(d_embed, nhead, dropout, causal)
        self.ln_2 = nn.LayerNorm(d_embed)
        self.ffn = FeedForward(d_embed, d_ffn, dropout)

    def forward(
        self,
        x: torch.Tensor,
        attention_mask: Optional[torch.Tensor] = None,
    ) -> torch.Tensor:
        """Apply one Transformer block."""
        x = x + self.attn(self.ln_1(x), attention_mask)  # [B, L, C]
        x = x + self.ffn(self.ln_2(x))  # [B, L, C]
        return x


class GPTModel(nn.Module):
    """GPT-style model that maps token sequences to per-token raw logits."""

    def __init__(
        self,
        layer_num: int,
        vocab_size: int,
        d_embed: int,
        d_ffn: int,
        dropout: float,
        nhead: int,
        out_size: Optional[int] = None,
        causal: bool = True,
    ) -> None:
        super().__init__()

        self.vocab_size = vocab_size
        self.d_embed = d_embed
        self.out_size = out_size if out_size is not None else vocab_size

        self.token_embed = nn.Embedding(vocab_size, d_embed)
        self.drop = nn.Dropout(dropout)

        self.blocks = nn.ModuleList(
            [
                GPTBlock(
                    d_embed=d_embed,
                    d_ffn=d_ffn,
                    dropout=dropout,
                    nhead=nhead,
                    causal=causal,
                )
                for _ in range(layer_num)
            ]
        )

        self.ln_f = nn.LayerNorm(d_embed)
        self.head = nn.Linear(d_embed, self.out_size)

        self.apply(self._init_weights)

    def forward(
        self,
        input_ids: torch.Tensor,
        attention_mask: Optional[torch.Tensor] = None,
    ) -> torch.Tensor:
        """Return raw per-token logits with shape [B, L, out_size]."""
        x = self.token_embed(input_ids)  # [B, L, C]
        x = self.drop(x)  # [B, L, C]

        for block in self.blocks:
            x = block(x, attention_mask)  # [B, L, C]

        x = self.ln_f(x)  # [B, L, C]

        logits = self.head(x)  # [B, L, out_size]
        return logits

    def _init_weights(self, module: nn.Module) -> None:
        if isinstance(module, nn.Linear):
            nn.init.normal_(module.weight, mean=0.0, std=0.02)
            if module.bias is not None:
                nn.init.zeros_(module.bias)

        elif isinstance(module, nn.Embedding):
            nn.init.normal_(module.weight, mean=0.0, std=0.02)
