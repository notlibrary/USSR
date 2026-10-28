# Sochi API

`$include "src/sochi/sochi.su"` gives a script the rules, the bots and the
network code. The game state lives in `sg_*` globals. A view is a vector
indexed by `V_*` constants.

## Entry point

`so_main(code): args` runs one command line, a vector of strings without
the program name, exactly as `ussr src/sochi/sochi.su ARGS...` would. It
returns 0 when the game ended or the player left, 1 when no table was
found, 2 on a usage error.

## The rules

| Function | Does |
|----------|------|
| `so_table(_): pool seed` | an empty table: names `""`, scores 0, phase `"lobby"` |
| `so_new_game(_):` | clears the scores and deals the first hand (set `sg_names` first) |
| `so_act(err): seat action` | applies an action; `""` when done, else the reason it was refused |
| `so_legal(v): seat` | the actions seat may take now (empty when it is not its turn) |
| `so_ctl(seat):` | who must act: the player to move, or the lone whister for the open hand; -1 for nobody |
| `so_view(v): seat` | what seat may see (below) |
| `so_bot(action): view` | a bot's action for that view, `""` when it has none |
| `so_settle(v):` | final balances in thirds of a whist, one per seat |
| `so_score_contract(_):` `so_score_misere(_):` `so_score_raspasy(_):` | score the hand in `sg_*` |
| `so_pool_add(_): seat n` | n points to a pool, a full pool helping the others |

Actions are strings, the same ones the network carries:

| Action | Phase |
|--------|-------|
| `b:6s` `b:10nt` `b:mis` `b:pass` | `bid` |
| `d:7s 8d` | `discard` (the declarer, 12 cards) |
| `c:7h` | `contract` |
| `w:whist` `w:pass` `w:half` | `whist`, `whist2` (the first defender again after a half-whist) |
| `p:Qh` | `play` |
| `n:` | `done`: ready for the next hand (all three deal it) |
| `t:text` | any time: a chat line |

The phases are `lobby bid discard contract whist whist2 play done over`.

Cards are 0..31: suit × 8 + rank, with suits ♠ ♣ ♦ ♥ = 0..3 and ranks
7 8 9 10 J Q K A = 0..7. `so_card(s): c` gives `"Qh"`, and
`so_parse_card(c): text` turns `"Qh"`, `"10s"`, `"ts"` or `"Q♥"` back into
a card, or -1. A bid is the order number `2 × ((level − 6) × 5 + strain)`
with strains ♠ ♣ ♦ ♥ NT = 0..4; misère is 29, a pass -1 and no bid yet -2.
The helpers are `so_bid_name`, `so_bid_show`, `so_parse_bid`, `so_level`,
`so_strain`, `so_value` and `so_obligation`.

## State

| Global | Holds |
|--------|-------|
| `sg_names`, `sg_kind` | nicknames; `local`, `remote` or `bot` |
| `sg_pool`, `sg_mount` | three pools and mountains |
| `sg_whist` | 9 numbers: `sg_whist[a × 3 + b]` = whists a wrote on b |
| `sg_psize`, `sg_dealer`, `sg_handno`, `sg_streak` | pool size, dealer, hand number, raspasy in a row |
| `sg_hands`, `sg_talon`, `sg_discard` | cards (sorted vectors) |
| `sg_phase`, `sg_turn` | the phase and the seat whose card or bid is next |
| `sg_bids`, `sg_high`, `sg_hseat` | each seat's last bid, the highest and its bidder |
| `sg_decl`, `sg_contract`, `sg_trump` | declarer (-1 on raspasy), contract, trump suit (4: none) |
| `sg_wst`, `sg_open` | `"whist"`, `"pass"`, `"half"` per seat; the open hand's seat (-2: both defenders, misère) |
| `sg_trick`, `sg_tseats`, `sg_tricks`, `sg_trickno` | the trick so far, who played it, tricks taken, tricks done |
| `sg_log`, `sg_logn`, `sg_chat`, `sg_result`, `sg_last` | recent events and their count, chat, last hand's score, last trick |
| `sg_final` | settlement once the game is over |

## A view

`so_view(v): seat` is a vector, read with `at(x): v V_HAND`. It holds
`V_SEQ V_SEAT V_NAMES V_KIND V_ONLINE V_POOL V_MOUNT V_WHIST V_PSIZE
V_DEALER V_HANDNO V_PHASE V_TURN V_CTL V_HAND V_OTHERS V_COUNTS V_BIDS
V_HIGH V_DECL V_CONTRACT V_TRUMP V_WST V_OPEN V_TALON V_DISCARD V_TRICK
V_TSEATS V_LEAD V_TRICKS V_TRICKNO V_LAST V_LOG V_LOGN V_CHAT V_RESULT
V_READY V_LEGAL V_ACTID V_STREAK V_ERR V_FINAL`.

- `V_OTHERS` holds a hand only when it is open (open whist, misère
  defenders); otherwise it is `null`.
- `V_TALON` shows the talon while the declarer takes it, and the talon
  card of a raspasy trick.
- `V_DISCARD` is shown to the declarer only.
- `V_LEGAL` is `so_legal` for that seat.

`so_render(lines): view` draws the screen, and `so_line_action(a): view
line` turns a typed line into an action (`"!"` and a message when it is
not one).

## The protocol

UDP datagrams of text, each `SOCHI1 VERB rest`:

| Datagram | From | Meaning |
|----------|------|---------|
| `FIND` | player, broadcast | is a table there? |
| `TABLE info` | host | `Ivan (2/3, pool 10)`, `, playing` once dealt |
| `JOIN nick` | player | a seat please; the same nick takes its seat back |
| `WELCOME seat lastid` | host | seated; action ids continue after lastid |
| `FULL` | host | no seat |
| `VIEW seq uno` | host | the player's view as UNO text, sent again until acked |
| `ACK seq` | player | view seq arrived |
| `ACT id action` | player | an action, sent again until a view shows `V_ACTID` ≥ id |
| `PING` | player | still here (about every 2 s; 15 s of silence shows offline) |
| `BYE` | either | leaving; the host's BYE ends the table |

## Native builtins it stands on

```
socket_udp(h): [port [ip]]            # bound UDP socket, broadcast on; -1 if the port is taken
socket_sendto(n): h host port data    # one datagram to host:port (255.255.255.255 broadcasts)
socket_recvfrom(v): h [ms]            # [text ip port], null if none came within ms, -1 on error
terminal_key_ready(v): [ms]           # 1 when terminal_getch has a key within ms
```
