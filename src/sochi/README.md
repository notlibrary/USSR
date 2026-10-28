# Sochi

Preferans for three players, written in USSR and played over the
network, by the Sochi ("Sochinka") rules. There is no server. The first
program to open a table is its host and keeps the game. The other two
find it on the LAN by broadcast, or join it by IP address or host name
(WAN), all on UDP port 33333. Every player picks a nickname, and the three
share a small chat while they play.

```bash
ussr src/sochi/sochi.su                     # nickname, then the tables on the LAN
ussr src/sochi/sochi.su host                # open a table here
ussr src/sochi/sochi.su join 192.168.1.20   # join a table by address
ussr src/sochi/sochi.su host --bots 2       # play against two bots
```

```
Sochi preferans · pool 10 · hand 3 · dealer Petr

  player        pool mount  whists on   Ivan   Petr  Masha
  Ivan             4     2                 -     12      0
  Petr             2     0                 0      -      8
> Masha            0     6                20      4      -

Contract: Petr 7♥   Masha whist   Ivan pass
Ivan (open): ♠ K 9   ♣ A 10 8   ♦ Q 7   ♥ -
Trick 4:  Masha 10♦
Last: Trick 3: Petr A♥ Masha 7♥ Ivan 9♥ - Petr
Tricks:  Ivan 0  Petr 2  Masha 1

Your cards: ♠ A Q 10   ♣ K   ♦ A 9 8   ♥ -

  Petr plays 7♥
  Masha: whist
  Ivan: pass
  Ivan lays the cards open
-- chat --
Petr: who dealt this?
Masha: you did

Play for Ivan: Qd 7d   ('text to chat)
> 
```

## Playing

Type moves at the `>` prompt. The bottom line always says what is
expected and lists the legal cards:

| When | Type |
|------|------|
| bidding | `6s` `6c` `6d` `6h` `6nt` ... `10nt`, `mis`, `pass` |
| after taking the talon | the two cards to discard: `7s 8d` |
| naming the contract | a bid no lower than yours: `7h` |
| whisting | `whist`, `pass`, or `half` (half-whist, on 6 only) |
| playing | a card: `Qh`, `10s` (or `ts`), `A♦` |
| between hands | Enter |

A line that starts with `'` goes to the chat. When it is not your turn,
anything you type goes to the chat. `/help` lists the moves and `/quit`
leaves. Suits show as ♠ ♣ ♦ ♥ on a terminal; `--ascii` uses `s c d h`.

## The rules (Sochi)

- **Cards.** 32 cards, 7 to Ace, in spades, clubs, diamonds and hearts, with
  no jokers. Each player gets 10 and 2 go to the talon. The dealer changes
  every hand; forehand (left of the dealer) bids first and leads the first
  trick.
- **Bidding.** 6♠ < 6♣ < 6♦ < 6♥ < 6NT < 7♠ ... < 10NT, with misère between
  8NT and 9♠. A bid must beat the last one, and a pass is final. Misère is
  allowed only as a player's first bid, and its bidder cannot bid again.
- **Talon.** The declarer takes the talon in view of all, discards two
  cards and names a contract no lower than the bid. Misère is played with
  no trumps, takes no whist, and its two defenders play with their cards
  open.
- **Whist.** Whist is responsible. The defenders owe 4 tricks on 6, 2 on 7
  and 1 on 8, 9 or 10. When both whist, each owes half (rounded down). A
  half-whist is allowed on 6 only, when the first defender passed; that
  defender may then whist after all. A lone whister plays the passer's
  hand, which is laid open.
- **Raspasy.** When all pass, there are no trumps and everyone avoids
  tricks. The two talon cards set the suit of tricks 1 and 2, and forehand
  plays first to them. Each trick costs 1 on the mountain, then 2 and 3
  on the 2nd and 3rd raspasy in a row. A player who takes no tricks
  writes 1 in the pool.
- **Scores.** Contracts are worth 6:2, 7:4, 8:6, 9:8, 10:10 and misère 10.
  - A made contract goes to the declarer's pool, and each whister writes
    value × tricks in whists on the declarer.
  - Each undertrick costs the declarer the value on the mountain, and each
    whister also gets value × undertricks in whists.
  - A whister short of their share, when the whisters together took less
    than they owe, gets half the value per missing trick on the mountain.
  - An unwhisted contract counts as made. A half-whister gets the whists
    for half the obligation.
  - Misère failed: 10 per trick on the mountain.
- **Game end.** The game ends when every pool reaches the pool size
  (`--pool`, default 10). Points a full pool cannot take help fill the
  other pools, at 10 whists per point on that player; anything left over
  becomes 5 whists per point on each of the other two. In the settlement,
  an unclosed pool goes to the mountain, and mountains are evened out at
  10 whists per point. The final score is each player's whist balance.

## Options

| Option | Does |
|--------|------|
| `--name NICK` | your nickname (else it asks) |
| `--pool N` | pool size, default 10 |
| `--bots N` | the host fills N seats with bots (0 to 2) |
| `--bot` | a bot plays this seat (for tables short of a player) |
| `--fast` | bots move without a pause |
| `--port P` | another UDP port than 33333 |
| `--seed N` | repeatable deals |
| `--ascii` | `s c d h` for suits |

`join ADDRESS` takes an IP, a host name or `host:port`. Plain `join` joins
the first table it finds on the LAN.

## Network

The host binds UDP port 33333 on every interface. A second host on the
same computer finds the port taken and joins the first table instead.
Players looking for a table broadcast to 255.255.255.255:33333 and also
ask 127.0.0.1. On Windows, the first host may trigger a firewall prompt
for ussr.exe: allow it for private networks so LAN players can reach the
table. For WAN play, the host forwards UDP 33333 on the router, and the
others `join` its public address.

The host keeps the game and sends every player only what that player may
see. Lost datagrams are sent again, so a short network hiccup only slows
the game down. A player who drops out can run `join` again with the same
nickname to take back their seat. [docs/API.md](docs/API.md) describes
the protocol and the functions for scripts.

## Test

`ussr tests/sochi_test.su` checks the rules without the network, against
`tests/expected/sochi_test.su.txt`, natively and through russia.su. See
[tests/TESTPLAN.md](tests/TESTPLAN.md).
