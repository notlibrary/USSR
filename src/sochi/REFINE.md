# Sochi — refinement ideas

**Today:** three-player Preferans by the Sochi rules over UDP port 33333
with no server. The first program to open a table hosts it, and tables
are found by LAN broadcast or joined by address. The rules cover bidding
with misère, talon and discard, responsible whist with half and open
whist, raspasy 1-2-3, pool, mountain, whists and settlement. It has
nicknames, chat, a terminal screen with suit symbols or `--ascii`, bots,
and rejoin with the same nickname.

## Rules and variants
- Other conventions as options: Leningradka (Piter), Rostov, Stalingrad
  (6♠ must be whisted), "gentleman's" vs responsible whist
- Raspasy variants: exits by 6-7-7, a fixed ceiling, an amnesty
- Configurable pool size, whist price and settlement rounding
- Four players with the dealer sitting out
- "Ten checked" and "no tricks after 3 passes" house rules
- Claiming the remaining tricks (open and claim) with automatic
  verification

## Bots
- Stronger bots: bidding by hand evaluation (expected tricks per suit),
  whist decisions by risk, and play by double-dummy search late in the
  hand
- Monte Carlo sampling of hidden hands for play decisions
- Difficulty levels, plus a bot that explains its move ("why")
- Hints for the human player (`/hint`)

## Interface
- A Petersburg GUI table with cards drawn on the canvas, click to play,
  and animations
- A Kremlin launcher and Mbltishi-friendly layout
- The full score sheet ("пуля") drawn as a classic diagram
- Undo of a misclick before others see it (within a second)
- Sound cues (optional)
- Colour themes and a larger-card mode
- Localisation: Russian UI next to English

## Networking
- Internet play: NAT traversal (UDP hole punching) or a small optional
  relay server
- Encrypted packets (DTLS or a shared-key AEAD) so hands cannot be
  sniffed on the LAN
- Fair dealing without trusting the host: a commit-reveal shuffle
  (mental poker)
- Spectators, joining read-only
- Reconnection after host loss: hand the game to another player with the
  log
- IPv6 and a configurable port

## Records
- Game log saved as text and in a replayable format
- Replay viewer: step through a finished hand with all cards shown
- Player statistics: games, wins, average result per hand type (stored
  in Dubna KV)
- Leaderboards for a LAN group
- Export of results to Siberia (CSV/FODS)

## Chat
- Emotes and canned phrases ("пас", "вист"), plus chat history scrolling
- Private messages between players
- Mute

## Tests
- Fuzz the packet parser
- Rule tests checked against published Sochi scoring tables
- Network tests with packet loss and reordering simulated
- Bot-versus-bot tournaments as regression for bot strength

## Teaching and analysis
- A tutorial mode that explains the rules step by step with a scripted
  deal
- A trainer: misère problems, "how many tricks is this hand worth",
  whist-or-pass quizzes
- After a hand: the double-dummy result and where the play lost tricks
- A hand editor to set up any deal and play it out against bots
- An odds helper: suit split probabilities for the cards still out

## Table and match options
- Time controls: a clock per move with auto-play of a forced card
- Auto-play of the only legal card (optional)
- Table passwords and a table name in the LAN list
- A lobby: several tables found on the LAN, with seats free and pool
  size shown
- Bots taking over a player who left, and giving the seat back on
  rejoin
- Tournament mode: several tables, a schedule and a combined score
- Save and resume an unfinished game from a file
- A turn bell or terminal title change when it is your move

## More games on the same engine
- Durak, Thousand (Тысяча), King and Belote on the 32/36-card deck,
  sharing the networking, chat and terminal code
- A generic card-table library split out of `sochi.su` (cards,
  shuffles, tricks, views, the UDP resend logic)
- A web client served by Ukraine, so a player needs only a browser
