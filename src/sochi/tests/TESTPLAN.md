# Sochi test plan

Automated: `ussr tests/sochi_test.su`, checked against
`tests/expected/sochi_test.su.txt` by `tests/run_all.sh`, which also runs
it through russia.su (about 30 s there). The test needs no network. Deals
come from sochi's own generator, so both interpreters deal the same cards.
It checks:

- card names and parsing (`Qh`, `10S`, `ts`, `7♣`, bad input), a hand by
  suits;
- every bid's name in order, misère between 8NT and 9♠; parsing bids and
  their values;
- a seeded deal: three hands of 10 and a talon of 2;
- bidding: out of turn, too low, misère as a first bid only, the misère
  bidder outbid can only pass, the declarer takes the talon (12 cards), a
  bad discard, a contract below the bid refused, both defenders pass and
  the contract counts as made, chat (a backtick cleaned), all three ready
  deal the next hand;
- half-whist on 6, then the first defender whists after all: the passer's
  hand is open and the whister plays it;
- legal cards: follow suit, must trump, anything, no trumps; trick
  winners; raspasy talon suits for tricks 1 and 2;
- scores: made with both whisting, a short whister, a lone whister and
  undertricks, 8NT with no obligation each, a lone whister short, a
  half-whist, 10 down 2, unwhisted; misère made and failed; raspasy
  1st and 3rd in a row; a full pool helping the others, and the remaining
  points becoming whists;
- the settlement in thirds and its formatting;
- a view through UNO and back unchanged, hidden hands, the hint line, and
  typed lines turned into actions;
- a whole game of three bots to the end, with the final score.

By hand (Windows 11, loopback and LAN broadcast):

- `sochi host --bot --pool 2 --fast`, `sochi join 127.0.0.1 --bot` and
  `sochi join --bot` (found by broadcast) play a game to the end, and all
  three print the same log and final score;
- a "human" seat driven through a pipe (`host --bots 2`) bids, discards,
  names a contract, whists, plays, chats and quits with `/quit`;
- the terminal screen in Windows Terminal: suits, redraws, the prompt,
  Ctrl-C restoring the console.
