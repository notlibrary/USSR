# Changelog

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
