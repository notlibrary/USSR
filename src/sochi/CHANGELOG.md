# Changelog

## 0.4.0
- The window plays on the LAN.
  - `gui.su --host [--bots N]` opens a table on UDP port 33333 (`--port P`)
    with this window in seat 0. Terminal players (`sochi join`), terminal
    bots (`sochi join --bot`) and other windows can join it. While the
    table waits, **Fill with bots** seats bots in the empty places and
    starts.
  - `gui.su --join [ADDRESS]` joins the first table found on the LAN, or
    the one at `ADDRESS` (`host` or `host:port`). The window draws the
    views the host sends and sends the moves you click; they are resent
    until the host has them, as at the terminal.
  - The table is drawn from your seat: the others sit left and right, and
    an empty seat or an offline player is marked.
- `tests/sochi_lan/run.sh` runs both directions on 127.0.0.1: a window
  hosting two terminal bots, and a window joining a terminal host. It is
  run by hand, since it needs UDP ports.

## 0.3.0
- `gui.su`: Sochi at a Petersburg table, you against two bots.
  - Cards are drawn on the canvas: big rank letters (7 8 9 10 J Q K A)
    and filled spade, club, diamond and heart icons, black or red.
  - Bids, contracts and whist choices are buttons (bids show their level
    and suit icon). To discard, click two cards, then Discard. To play,
    click a card; the cards you may play have a gold edge and the rest
    are greyed.
  - The bots' hands are face down unless open. The trick or the last one
    is in the middle, with the talon when it shows. A score sheet shows
    the pool, mountain, whists and money.
  - Enter goes to the next hand; Escape closes the window.
  - Callbacks only queue clicks; the main loop plays, moves the bots and
    redraws.
- `tests/gui/sochi_gui_test.su` plays a whole short game through
  injected canvas clicks.

## 0.2.0
- Refine pass 1, in `more.su`:
  - An odds helper: `sochi odds N A B` and `/odds SUIT` give how the
    cards of a suit split between two hands (hypergeometric, in tenths
    of a percent).
  - What a hand is worth: `sochi worth CARDS [TRUMP]` and `/worth`
    estimate tricks per suit from top sequences, guarded kings and
    queens, and long trumps.
  - Save and resume: `/save FILE` writes every value of the game as UNO.
    `sx_load` reads it back; `sochi show FILE` prints its scores and
    settlement.
  - The log as text (`/log FILE`) and the settlement as CSV for Siberia
    (`/csv FILE`, `sochi csv FILE OUT`).
  - A hand editor: `sx_set_deal("seat1: ...; seat2: ...; seat3: ...;
    talon: ...")` checks 10 cards per seat, 2 in the talon and no card
    dealt twice.
- Tests: `tests/sochi_more_test.su`, including 300 random packets through
  the packet parser.

## 0.1.0
- First version: Preferans for three by the Sochi rules (bidding with
  misère, talon and discard, responsible whist with half-whist and open
  whist, raspasy with 1-2-3 progression, pool, mountain and whists, a full
  pool helping the others, settlement).
- Peer to peer over UDP port 33333: any copy hosts or joins; tables found
  by LAN broadcast, or joined by IP, host name or `host:port`. The host
  resends views and players resend moves until each side has them, and a
  player can rejoin with the same nickname.
- Nicknames, a chat for the three players, a terminal screen with ♠ ♣ ♦ ♥
  (`--ascii` for letters), bots (`--bot`, `--bots N`, `--fast`).
- Stands on new ussr builtins: `socket_udp`, `socket_sendto`,
  `socket_recvfrom` and `terminal_key_ready`.
