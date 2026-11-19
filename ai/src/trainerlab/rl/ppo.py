"""Self-play with PPO: the network plays against itself and its past versions, and learns from it.

Each update plays a rollout of `STEPS` decisions in every battle of the vector environment. The
learner plays side 0; side 1 is, for the whole rollout, either the learner itself or one of its
past versions, kept every `SNAPSHOT_EVERY` updates (at most `POOL` of them): a network that only
ever met itself could learn to beat one way of playing. Both sample their options from their
policy.

Only the learner's decisions are learned from. A step where it had nothing to choose (the foe
replacing a Pokemon) passes its reward to the learner's previous decision. Advantages come from
GAE; the update is PPO's clipped objective, with a value loss and an entropy bonus. Settings
fixed before any measurement.

Needs PyTorch: pip install -e "ai/.[rl]".
"""

import copy
import random
from collections.abc import Callable
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt
import torch
from torch.nn import functional

from trainerlab.learning.env import Observation, VecEnv
from trainerlab.rl.net import PolicyValueNet

BATTLES = 256
STEPS = 128  # decisions per battle and rollout
GAMMA, LAMBDA = 0.99, 0.95
CLIP = 0.2
EPOCHS = 4  # passes over each rollout
MINIBATCH = 4096
RATE = 3e-4
VALUE_WEIGHT = 0.5
ENTROPY_WEIGHT = 0.01
MAX_NORM = 0.5
SNAPSHOT_EVERY = 10
POOL = 10
SELF_SHARE = 0.5  # rollouts against the learner itself, the rest against a past version


@dataclass(frozen=True)
class Progress:
    update: int
    decisions: int  # learner decisions so far
    battles: int  # battles finished so far
    win_rate: float  # learner's share of wins (draws half) in this rollout's finished battles
    policy_loss: float
    value_loss: float
    entropy: float


def _inputs(obs: Observation, side: int, on: torch.device) -> tuple[torch.Tensor, ...]:
    return (
        torch.from_numpy(obs.pokemon[:, side]).to(on),
        torch.from_numpy(obs.ids[:, side]).long().to(on),
        torch.from_numpy(obs.field[:, side]).to(on),
        torch.from_numpy(obs.mask[:, side]).to(on),
    )


@torch.no_grad()
def _act(
    net: PolicyValueNet, inputs: tuple[torch.Tensor, ...]
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Samples an option per battle; its log-probability, and the value. 0 where none is legal."""
    logits, value = net(*inputs)
    has_choice = inputs[3].sum(-1) > 0
    logits = torch.where(has_choice[:, None], logits, torch.zeros_like(logits))
    log_probs = functional.log_softmax(logits, dim=-1)
    sampled = torch.multinomial(log_probs.exp(), 1).squeeze(-1)
    action = torch.where(has_choice, sampled, torch.zeros_like(sampled))
    return action, log_probs.gather(1, action[:, None]).squeeze(-1), value


def advantages(
    decided: npt.NDArray[np.bool_],
    rewards: npt.NDArray[np.float32],
    done: npt.NDArray[np.bool_],
    values: npt.NDArray[np.float32],
    last_value: npt.NDArray[np.float32],
) -> npt.NDArray[np.float32]:
    """GAE over the learner's decisions only, arrays [steps, battles].

    A reward or an end that comes at a step without a decision belongs to the decision before.
    """
    steps, battles = decided.shape
    reward_since, ended_since = np.zeros(battles, np.float32), np.zeros(battles, bool)
    next_value, next_advantage = last_value.astype(np.float32), np.zeros(battles, np.float32)
    result = np.zeros((steps, battles), np.float32)
    for t in reversed(range(steps)):
        reward_since += rewards[t]
        ended_since |= done[t]
        d = decided[t]
        go_on = (~ended_since).astype(np.float32)
        delta = reward_since + GAMMA * next_value * go_on - values[t]
        advantage = delta + GAMMA * LAMBDA * go_on * next_advantage
        result[t] = np.where(d, advantage, 0.0)
        next_value = np.where(d, values[t], next_value)
        next_advantage = np.where(d, advantage, next_advantage)
        reward_since = np.where(d, 0.0, reward_since).astype(np.float32)
        ended_since = np.where(d, False, ended_since)
    return result


def selfplay(
    net: PolicyValueNet,
    updates: int,
    *,
    seed: int = 0,
    on: torch.device | None = None,
    battles: int = BATTLES,
    steps: int = STEPS,
    report: Callable[[Progress, PolicyValueNet], None] | None = None,
) -> PolicyValueNet:
    """Trains `net` for `updates` rollouts of self-play; returns it, on the CPU."""
    on = on or torch.device("cpu")
    torch.manual_seed(seed)
    picker = random.Random(seed)
    net = net.to(on)
    optimizer = torch.optim.Adam(net.parameters(), lr=RATE)
    env = VecEnv(battles, seed=seed)
    pool: list[PolicyValueNet] = []
    decisions = 0
    for update in range(1, updates + 1):
        opponent = net if not pool or picker.random() < SELF_SHARE else picker.choice(pool)
        net.eval()
        stored: dict[str, list[torch.Tensor]] = {
            k: [] for k in ("pokemon", "ids", "field", "mask", "a", "p", "v")
        }
        decided, rewards, done = [], [], []
        finished, score = 0, 0.0
        obs = env.observe()
        for _ in range(steps):
            mine, theirs = _inputs(obs, 0, on), _inputs(obs, 1, on)
            action, log_prob, value = _act(net, mine)
            foe_action, _, _ = _act(opponent, theirs)
            stored["a"].append(action)
            stored["p"].append(log_prob)
            stored["v"].append(value)
            decided.append(obs.mask[:, 0].sum(-1) > 0)
            for key, tensor in zip(("pokemon", "ids", "field", "mask"), mine, strict=True):
                stored[key].append(tensor)
            actions = torch.stack([action, foe_action], dim=1).cpu().numpy().astype(np.int32)
            reward, end = env.step(actions)
            rewards.append(reward[:, 0])
            done.append(end)
            finished += int(end.sum())
            score += float(((reward[end, 0] + 1) / 2).sum())
            obs = env.observe()
        _, _, last_value = _act(net, _inputs(obs, 0, on))

        decided_a = np.stack(decided)
        values = torch.stack(stored["v"]).cpu().numpy()
        adv = advantages(
            decided_a, np.stack(rewards), np.stack(done), values, last_value.cpu().numpy()
        )
        keep = torch.from_numpy(decided_a.reshape(-1)).to(on)
        flat = {
            key: torch.cat(stored[key])[keep]
            for key in ("pokemon", "ids", "field", "mask", "a", "p", "v")
        }
        advantage = torch.from_numpy(adv.reshape(-1)).to(on)[keep]
        returns = advantage + flat["v"]
        advantage = (advantage - advantage.mean()) / (advantage.std() + 1e-8)
        losses = _learn(net, optimizer, flat, advantage, returns)
        decisions += int(keep.sum())

        if update % SNAPSHOT_EVERY == 0:
            pool.append(copy.deepcopy(net).eval())
            del pool[:-POOL]
        if report is not None:
            win_rate = score / finished if finished else float("nan")
            report(Progress(update, decisions, env.finished, win_rate, *losses), net)
    return net.cpu()


def _learn(
    net: PolicyValueNet,
    optimizer: torch.optim.Optimizer,
    batch: dict[str, torch.Tensor],
    advantage: torch.Tensor,
    returns: torch.Tensor,
) -> tuple[float, float, float]:
    net.train()
    n = len(advantage)
    totals = np.zeros(3)
    rounds = 0
    for _ in range(EPOCHS):
        order = torch.randperm(n, device=advantage.device)
        for start in range(0, n, MINIBATCH):
            i = order[start : start + MINIBATCH]
            logits, value = net(
                batch["pokemon"][i], batch["ids"][i], batch["field"][i], batch["mask"][i]
            )
            log_probs = functional.log_softmax(logits, dim=-1)
            taken = log_probs.gather(1, batch["a"][i][:, None]).squeeze(-1)
            ratio = torch.exp(taken - batch["p"][i])
            clipped = torch.clamp(ratio, 1 - CLIP, 1 + CLIP)
            policy_loss = -torch.min(ratio * advantage[i], clipped * advantage[i]).mean()
            value_loss = functional.mse_loss(value, returns[i])
            entropy = -(log_probs.exp() * log_probs).sum(-1).mean()
            loss = policy_loss + VALUE_WEIGHT * value_loss - ENTROPY_WEIGHT * entropy
            optimizer.zero_grad()
            loss.backward()  # type: ignore[no-untyped-call]
            torch.nn.utils.clip_grad_norm_(net.parameters(), MAX_NORM)
            optimizer.step()
            totals += [policy_loss.item(), value_loss.item(), entropy.item()]
            rounds += 1
    mean = totals / max(rounds, 1)
    return float(mean[0]), float(mean[1]), float(mean[2])
