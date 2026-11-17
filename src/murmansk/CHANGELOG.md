# Changelog

## 0.2.0
Refine pass 1 from REFINE.md. The rule language is in `murmansk.su`; rule
management, state, observability and converters are in `more.su`.
- Rules: port lists (`80,443,8000-8099`), service names (`ssh`, `https`),
  address lists, named sets (`set web = 80,443`, used as `$web`), both
  ends in one rule (`from X port P to Y`), `reject`, `log`, `limit 20/s
  burst 40`, `perip N`, `during 08:00-18:00`, `until 18:00`, ICMP types
  (`icmp echo-request`), TCP `flags syn,!ack`, `for PROGRAM`, `group G`,
  names (`web-in: allow ...`) and comments kept with each rule.
- `murmansk_decide` takes a packet btree (both addresses and ports, ICMP
  type, flags, program, bytes) and gives allow, deny or reject.
- Rules are indexed per direction and protocol instead of scanned in full.
- Managing: `murmansk_insert`, `murmansk_move`, delete by name,
  `murmansk_test` (which rule decides), `murmansk_replace` (all or
  nothing), `murmansk_apply_timed` and `murmansk_confirm`,
  `murmansk_zero`, and `murmansk_lint` (errors and rules that never
  decide).
- State: a connection table (`murmansk_conns`) with timeouts per
  protocol, port-scan blocking (`scan N S T`), blocks from other programs
  (`murmansk_block`), a blocklist looked up by prefix length, `antispoof`,
  an `audit` trail, `forward` lines, malformed-packet counters.
- Observability: byte counters per rule, top sources and ports, a
  `history N` of drops, a rotated `logfile`, and pcap capture of drops.
- Profiles (`home`, `public`, `server`, or your own), groups switched on
  and off, `include` of a file or a folder of `*.rules`, and iptables
  output and input for the subset both have.
- Moscow's `fw` command takes `insert`, `move`, `delete NAME`, `test`,
  `conns`, `zero`, `top`, `lint`, `confirm`, `profile`, `group`, `block`,
  `unblock` and `iptables`; restoring rules after a program keeps the
  connection table and blocks.
- `tests/murmansk_more_test.su`: 106 checks, including a reload property
  test, a parser fuzz test and 1 000 rules; `tests/murmansk_test.su` is
  unchanged.

## 0.1.0
- First version: ordered allow and deny rules per direction with
  policies, ports, networks, hit counts, the last 16 drops, the `fw`
  command and `/etc/murmansk.rules`.
