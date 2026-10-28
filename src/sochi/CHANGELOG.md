# Changelog

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
