# Chaos Poker — Rules & IO Specification

A modified Texas Hold'em poker game with two additional mechanics: **community card voting** and **hole card swapping**.

---

## 1. Overview

Chaos Poker is a multi-player poker game based on Texas Hold'em. Players are dealt two hole cards and share community cards on the board. The game introduces two twists:

1. After each street (flop/turn/river) is dealt, players may **swap** their hole cards for random replacements at a cost.
2. After the swap phase, players **vote** (with money) on whether to keep or redraw the community cards for that street.

The game is played over multiple hands. Players are eliminated when they run out of chips. The last player standing wins.

---

## 2. Setup

### 2.1 Players
- Minimum 2 players, no fixed upper limit.
- Each player is assigned a seat number `0` through `N-1`.

### 2.2 Chips
- All players start with the same number of chips (announced at the start of the game).

### 2.3 Blinds
- The game uses a small blind / big blind structure.
- Initial small blind = 1, big blind = 2.
- **Blind escalation**: After each full revolution of the dealer button (i.e., after every player has been the dealer once), the small blind is recalculated as:
  ```
  new_small_blind = max(previous_small_blind, min(2 * previous_small_blind, ceil(min_stack / 3)))
  ```
  where `min_stack` is the smallest chip stack among non-eliminated players. The big blind is always `2 × small_blind`.
- This ensures blinds never decrease, try to double each revolution, but are capped so they don't grow large too quickly.

### 2.4 Swap Costs
- At the start of the game, four swap cost **multipliers** are announced: one each for pre-flop, flop, turn, and river.
- The actual swap cost for a street is `multiplier × current_small_blind`.
- As blinds escalate, swap costs increase proportionally.

### 2.5 Deck
- Standard 52-card deck, reshuffled each hand.

---

## 3. Hand Flow

Each hand proceeds through the following phases:

```
Deal hole cards
  → Swap phase (pre-flop cost)
  → Pre-flop betting round
  → Deal flop (3 cards)
    → Swap phase (flop cost)
    → Vote phase
    → Betting round
  → Deal turn (1 card)
    → Swap phase (turn cost)
    → Vote phase
    → Betting round
  → Deal river (1 card)
    → Swap phase (river cost)
    → Vote phase
    → Betting round
  → Showdown
```

### 3.1 Dealer Button and Blinds
- The dealer button rotates clockwise each hand.
- The player to the left of the dealer posts the small blind.
- The player two seats to the left of the dealer posts the big blind.
- If only 2 players remain, the dealer posts the small blind and the other player posts the big blind.

### 3.2 Dealing
- Each player is dealt 2 hole cards face-down.
- Community cards are dealt face-up on the board at the appropriate street.

---

## 4. Betting Rounds

Standard Texas Hold'em no-limit betting rules apply:

- Players act in clockwise order starting from the player to the left of the big blind (pre-flop) or the player to the left of the dealer (post-flop).
- On each turn, a player may:
  - **FOLD** — forfeit the hand.
  - **CHECK** — pass action (only if no bet to call).
  - **CALL** — match the current bet.
  - **RAISE `amount`** — raise the total bet to `amount`. The minimum raise is the size of the previous raise (or the big blind if no prior raise). The maximum is the player's remaining chips (all-in).
- A betting round ends when all active players have acted and all bets are equal (or players are all-in).
- A player who is all-in remains in the hand but cannot act further in betting rounds.

---

## 5. Swap Phase

A swap phase occurs at each street (including pre-flop). Each active (non-folded, non-all-in) player may swap hole cards.

The swap phase consists of multiple **swap rounds**:

1. In each swap round, all eligible players are sent a `SWAP_PROMPT` simultaneously.
2. Each player may swap **exactly one** hole card (by index) or choose to `STAY`.
3. Players who swap pay the swap cost for the current street. This money goes into the pot.
4. Swapped cards are replaced with cards drawn randomly from the remaining deck (cards not in any player's hand or on the board). The old card is discarded (not returned to the deck).
5. After a swap round completes:
   - Any player who chose `STAY` is **removed from further swap rounds** for this phase. They receive no more `SWAP_PROMPT` messages until `SWAP_DONE`.
   - If at least one player swapped, another swap round begins with only those players who swapped in the previous round.
   - If no player swapped (all chose `STAY`), the swap phase ends.
6. A player cannot swap if they do not have enough chips to pay the cost.

---

## 6. Vote Phase

After the swap phase, active (non-folded) players vote on whether to keep the current community cards for this street.

- Each player submits a vote: **YES** (keep the cards) or **NO** (redraw the cards), along with an **amount** of chips to wager on their vote.
- The minimum vote amount is 0 (effectively abstaining while still choosing a side).
- The maximum vote amount is the player's remaining chips.
- All vote money goes into the pot regardless of the outcome.
- The engine sums up the total chips wagered on YES and the total on NO.
  - If YES total >= NO total (ties go to YES): the community cards **stay**.
  - If NO total > YES total: the community cards for this street are **discarded and redrawn** from the remaining deck.
- After the vote resolves, all players are told:
  - The total money on YES and the total money on NO.
  - Whether the cards were kept or redrawn.
  - If redrawn, the new community cards.
- Individual votes are **secret** — no player is told how any other player voted.

---

## 7. Showdown

After the final betting round:

- All remaining (non-folded) players reveal their hole cards.
- The best 5-card hand from each player's 2 hole cards + 5 community cards wins.
- Standard poker hand rankings apply (Royal Flush > Straight Flush > Four of a Kind > Full House > Flush > Straight > Three of a Kind > Two Pair > One Pair > High Card).
- In case of a tie, the pot is split equally among tied players (remainder chips go to the player closest to the left of the dealer).
- Side pots are resolved in standard fashion when players are all-in for different amounts.

---

## 8. Elimination and Winning

- A player with 0 chips at the start of a hand is eliminated.
- The game continues until only one player remains, or until **200 revolutions** of the dealer button have completed.
- If the game ends by revolution limit, the player with the most chips wins. If multiple players are tied for the most chips, the game is a tie.

---

## 9. IO Specification

Communication between the engine and bots uses **plain text over stdin/stdout**, one message per line.

### 9.1 Notation

**Cards** are represented as two characters: rank then suit.
- Ranks: `2`, `3`, `4`, `5`, `6`, `7`, `8`, `9`, `T`, `J`, `Q`, `K`, `A`
- Suits: `s` (spades), `h` (hearts), `d` (diamonds), `c` (clubs)
- Examples: `As` (Ace of spades), `Td` (Ten of diamonds), `2c` (Two of clubs)

**Players** are identified by their seat number (0-indexed integer).

**Chip amounts** are integers.

---

### 9.2 Engine → Bot Messages

These messages are sent to all bots (broadcast) unless noted otherwise. Each message is a single line.

#### `GAME_START`
Sent once at the start of the game.
```
GAME_START <num_players> <your_seat> <starting_chips> <preflop_swap_cost> <flop_swap_cost> <turn_swap_cost> <river_swap_cost>
```
Example:
```
GAME_START 6 2 1000 5 15 25 50
```
Meaning: 6 players, you are seat 2, everyone starts with 1000 chips, swap cost multipliers are 5 (pre-flop), 15 (flop), 25 (turn), 50 (river). Blinds start at 1/2 and double each dealer revolution.

#### `HAND_START`
Sent at the start of each hand.
```
HAND_START <hand_number> <dealer_seat> <small_blind_seat> <big_blind_seat> <small_blind_amount> <big_blind_amount>
```
Example:
```
HAND_START 3 0 1 2 2 4
```

#### `CHIPS`
Sent at the start of each hand, reports all players' chip counts.
```
CHIPS <seat0_chips> <seat1_chips> ... <seatN_chips>
```
Eliminated players have `0`. Example:
```
CHIPS 950 1020 0 1030 500 500
```

#### `DEAL_HOLE`
Sent privately to each bot (each bot sees only their own cards).
```
DEAL_HOLE <card1> <card2>
```
Example:
```
DEAL_HOLE Ah Kd
```

#### `DEAL_FLOP`
```
DEAL_FLOP <card1> <card2> <card3>
```

#### `DEAL_TURN`
```
DEAL_TURN <card>
```

#### `DEAL_RIVER`
```
DEAL_RIVER <card>
```

#### `SWAP_PROMPT`
Sent to each active player at the start of a swap round.
```
SWAP_PROMPT <cost_per_card> <your_chips>
```
Example:
```
SWAP_PROMPT 15 940
```

#### `SWAP_RESULT`
Sent privately to the player who swapped, showing their new card.
```
SWAP_RESULT <new_card>
```
Example:
```
SWAP_RESULT Qh
```
After receiving `SWAP_RESULT`, the player will receive another `SWAP_PROMPT` for the next swap round (since they swapped, they remain eligible). Players who chose `STAY` receive no further messages until `SWAP_DONE`.

#### `SWAP_DONE`
Sent to all players when the swap phase ends.
```
SWAP_DONE
```

#### `VOTE_PROMPT`
Sent to each active player during the vote phase.
```
VOTE_PROMPT <your_chips>
```

#### `VOTE_RESULT`
Sent to all players after the vote.
```
VOTE_RESULT <yes_total> <no_total> <KEPT|REDRAWN>
```
Example:
```
VOTE_RESULT 120 85 KEPT
```

#### `REDRAW_FLOP`
If the flop was redrawn after a vote.
```
REDRAW_FLOP <card1> <card2> <card3>
```

#### `REDRAW_TURN`
```
REDRAW_TURN <card>
```

#### `REDRAW_RIVER`
```
REDRAW_RIVER <card>
```

#### `ACTION_PROMPT`
Sent to the player whose turn it is to act in a betting round.
```
ACTION_PROMPT <your_chips> <current_bet> <your_bet> <min_raise> <pot>
```
- `your_chips`: your remaining chips
- `current_bet`: the current highest bet on the table
- `your_bet`: how much you have already put in this betting round
- `min_raise`: minimum total bet if raising
- `pot`: total pot size

Example:
```
ACTION_PROMPT 500 20 10 30 45
```
Meaning: you have 500 chips, current bet is 20, you've put in 10 so far, minimum raise to 30, pot is 45.

#### `ACTION`
Broadcast to all players when any player acts.
```
ACTION <seat> <action> [<amount>]
```
Examples:
```
ACTION 3 FOLD
ACTION 1 CALL 20
ACTION 0 RAISE 60
ACTION 2 CHECK
ACTION 4 ALLIN 350
```

#### `SHOWDOWN`
Sent when hands are revealed at showdown.
```
SHOWDOWN <seat> <card1> <card2>
```
One line per player still in the hand.

#### `WINNER`
Sent when a pot (or side pot) is awarded.
```
WINNER <seat> <amount> <hand_rank>
```
- `hand_rank`: one of `ROYAL_FLUSH`, `STRAIGHT_FLUSH`, `FOUR_OF_A_KIND`, `FULL_HOUSE`, `FLUSH`, `STRAIGHT`, `THREE_OF_A_KIND`, `TWO_PAIR`, `ONE_PAIR`, `HIGH_CARD`

Example:
```
WINNER 2 350 FLUSH
```

#### `ELIMINATE`
Sent when a player is eliminated.
```
ELIMINATE <seat>
```

#### `GAME_OVER`
Sent when the game ends.
```
GAME_OVER <winning_seat>
GAME_OVER TIE <seat1> <seat2> [...]
```
If the game ends by revolution limit and top stacks are tied, the `TIE` form is used.

---

### 9.3 Bot → Engine Messages

Bots respond with a single line when prompted.

#### In response to `ACTION_PROMPT`:
```
FOLD
CHECK
CALL
RAISE <amount>
ALLIN
```
- `RAISE <amount>` — the total bet amount (not the increment). Must be >= `min_raise` and <= `your_chips + your_bet`.
- `ALLIN` — go all-in with remaining chips.

#### In response to `SWAP_PROMPT`:
```
SWAP <card_index>
STAY
```
- `SWAP 0` — swap your first hole card.
- `SWAP 1` — swap your second hole card.
- `STAY` — keep your current cards. You will not be prompted again this swap phase.

#### In response to `VOTE_PROMPT`:
```
VOTE YES <amount>
VOTE NO <amount>
```
- `amount` is the number of chips to wager on this vote (0 to your remaining chips).

---

### 9.4 Message Flow Example

A condensed example of one hand with 3 players:

```
Engine → All:     HAND_START 1 0 1 2 1 2
Engine → All:     CHIPS 1000 1000 1000
Engine → P0:      DEAL_HOLE As Kd
Engine → P1:      DEAL_HOLE 7h 7c
Engine → P2:      DEAL_HOLE Td 9d

  (pre-flop swap phase — swap round 1)
Engine → P0:      SWAP_PROMPT 5 1000
Engine → P1:      SWAP_PROMPT 5 1000
Engine → P2:      SWAP_PROMPT 5 1000
P0 → Engine:      STAY
P1 → Engine:      STAY
P2 → Engine:      SWAP 1
Engine → P2:      SWAP_RESULT Jd

  (swap round 2 — only P2 is eligible, P0 and P1 chose STAY)
Engine → P2:      SWAP_PROMPT 5 995
P2 → Engine:      STAY

  (all remaining players chose STAY, swap phase ends)
Engine → All:     SWAP_DONE

  (pre-flop betting)
Engine → P0:      ACTION_PROMPT 1000 2 0 4 3
P0 → Engine:      CALL
Engine → All:     ACTION 0 CALL 2
Engine → P1:      ACTION_PROMPT 999 2 1 4 5
P1 → Engine:      CALL
Engine → All:     ACTION 1 CALL 2
Engine → P2:      ACTION_PROMPT 995 2 2 4 9
P2 → Engine:      CHECK
Engine → All:     ACTION 2 CHECK

  (flop)
Engine → All:     DEAL_FLOP Ah 7d 3c

  (flop swap phase)
Engine → P0:      SWAP_PROMPT 15 998
Engine → P1:      SWAP_PROMPT 15 998
Engine → P2:      SWAP_PROMPT 15 993
P0 → Engine:      STAY
P1 → Engine:      STAY
P2 → Engine:      STAY
Engine → All:     SWAP_DONE

  (vote phase)
Engine → P0:      VOTE_PROMPT 998
Engine → P1:      VOTE_PROMPT 998
Engine → P2:      VOTE_PROMPT 993
P0 → Engine:      VOTE YES 50
P1 → Engine:      VOTE YES 30
P2 → Engine:      VOTE NO 20
Engine → All:     VOTE_RESULT 80 20 KEPT

  (post-flop betting)
Engine → P1:      ACTION_PROMPT 968 0 0 2 106
P1 → Engine:      CHECK
  ...
```

---

### 9.5 Timing and Errors

- Bots must respond within **10 milliseconds** per decision.
- If a bot exceeds the time limit **or** sends a malformed/invalid message, the bot is **auto-folded for the entire current hand**. Specifically:
  - The bot is treated as having folded immediately.
  - The bot receives no further prompts for the remainder of that hand.
  - The bot resumes normal play in the next hand.
- **All-error fold rule**: If every remaining player gets auto-folded due to IO errors, the pot is split equally among the players who errored in the **most recent phase** (swap, vote, or betting). Players who errored in an earlier phase do not receive a share. This prevents the pot from being lost entirely.

---

## 10. Summary of Differences from Standard Texas Hold'em

| Feature | Standard | Chaos Poker |
|---|---|---|
| Community cards | Fixed once dealt | Can be redrawn via vote |
| Hole cards | Fixed for the hand | Can be swapped at a cost (including pre-flop) |
| Voting | N/A | Money-weighted vote after each street |
| Swap costs | N/A | Per-card cost, varies by street, multiple of small blind |
| Blinds | Fixed or timed | Start 1/2, escalate each revolution capped by min stack |
| Invalid IO | N/A | Auto-fold for the hand |
| Response time | N/A | 10ms limit, auto-fold on timeout |
