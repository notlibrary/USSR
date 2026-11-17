# Murmansk — refinement ideas

**Today:** the firewall of Moscow's network stack (~490 lines). Rules are
read in order: `allow|deny in|out [tcp|udp|icmp|any] [port|lo-hi]
[from|to addr/bits]`, with a policy per direction. Established flows
bypass the rules. It counts hits, keeps the last 16 drops, and has the
`fw(_):` shell command and `/etc/murmansk.rules`.

**Refine pass 1 (0.2.0):** the rule language in `murmansk.su`, the rest in
`more.su`; [x] done, [~] partly done. See CHANGELOG.md and
`tests/murmansk_more_test.su`.

## Rule language
- [x] Port lists (`tcp 80,443,8000-8099`) and address lists
- [x] Named sets: `set web = 80,443`, `set lan = 10.0.2.0/24, 192.168.0.0/16`,
  used as `$web` and `$lan`
- [x] Both ends in one rule: `from X to Y`, and source ports
- [~] `reject` next to `deny`: answer with an RST or ICMP unreachable in
  place of a silent drop — partly: reject is a verdict of its own (murmansk_decide); the network stack does not send the RST or ICMP answer yet
- [x] `log` as a rule option (`allow log in tcp 22`) to log matches as well
  as drops
- [x] Rate limiting: `allow in tcp 8443 limit 20/s burst 40`, and a
  per-source connection limit
- [x] Time-based rules (`during 08:00-18:00`)
- Interface names, once Moscow has more than one interface
- [x] ICMP types (`icmp echo-request`)
- [~] TCP flags matching — partly: flags syn,!ack match a packet's flags in murmansk_decide; net.su does not pass them yet (a packet without flags counts as a SYN)
- [x] Comments kept with each rule and shown in the report
- IPv6 addresses and prefixes, once the stack has IPv6

## Rule management
- [x] `fw(_): "insert N rule"` and `fw(_): "move A B"`
- [x] Named rules: `fw(_): "delete web-in"`
- [x] A dry-run check: `fw(_): "test in tcp 10.0.2.15 8080"` prints which
  rule decides
- [x] Atomic replace: load a whole new rule file, roll back if it has errors
- [x] A timed apply: "keep for 60 s unless confirmed", so a remote session
  cannot lock itself out
- [x] Reset of counters without reloading
- [x] A rule-file syntax checker for use before `save`

## State and protection
- [x] An explicit connection table view: `fw(_): "conns"`
- [x] Timeouts per protocol for UDP pseudo-flows
- SYN flood protection (SYN cookies or a half-open limit)
- [x] Port-scan detection: many ports from one source leads to a temporary
  block
- [~] Fail2ban-style dynamic blocks driven by Novgorod or Ukraine (too many
  4xx from one IP) — partly: murmansk_block(ip seconds why) and murmansk_unblock for other programs; Novgorod and Ukraine do not call it yet
- [x] A blocklist file of addresses or prefixes, loaded quickly with a
  prefix trie

## NAT and forwarding
- [~] Port forwarding (`forward tcp 80 to 10.0.2.20:8080`) — partly: forward lines and murmansk_forward give where a port goes; nothing forwards packets yet
- Masquerading/NAT, if Moscow ever routes between interfaces

## Observability
- [~] A persistent drop log to `/var/log/murmansk.log` with rotation — partly: logfile PATH [BYTES] appends drops and rotates to PATH.1, with the hosted file calls (not Moscow's own disk)
- [~] Per-rule byte counters as well as packet hits — partly: a packet's bytes are counted per rule when murmansk_check_pkt gets them; net.su does not pass them yet
- [x] Statistics by top sources and top ports
- A Kremlin panel indicator and a Petersburg rules editor window

## Performance
- [x] Compile rules to a decision structure (per-protocol and port buckets)
  in place of a linear scan
- [~] Benchmarks for rule sets of 1 000+ rules — partly: a test with 1 000 rules checks the decisions; no timings

## Tests
- [x] Property tests: a rule set and its `murmansk_text` reloaded decide
  the same
- [x] A fuzz test for rule parsing
- An integration test inside Moscow with a QEMU guest pinging and
  connecting

## Application awareness
- [~] Rules by program: `allow out tcp 443 for lenin`, using the process
  that opened the socket — partly: for PROGRAM matches a packet's prog; the stack does not say which program opened the socket yet
- Ask mode on a desktop: an unknown outgoing connection raises a
  Kremlin dialog (allow once, always, deny)
- [x] Profiles: `home`, `public`, `server`, switched with one command
- [x] Rule groups that can be enabled and disabled as a unit
- [x] Service names for ports (`ssh`, `https`) from a small table
- Host names in rules, resolved at load time and refreshed
- Country or ASN blocks from an offline prefix table

## Deeper inspection
- [~] Fragment handling policy and a drop of malformed headers, counted
  apart from rule drops — partly: murmansk_malformed(why) counts malformed packets apart from rule drops; no fragment policy
- [x] Anti-spoofing: drop packets that claim a local or private source on
  the wrong side
- [x] Logging of the first packet of every allowed connection (an audit
  trail), as an option
- [~] A pcap writer for dropped packets (`fw(_): "capture on"`) — partly: murmansk_capture(path) writes dropped packets that carry their bytes as pcap; net.su does not pass the bytes yet
- Outgoing rules consulted for UDP sends and DNS, as they are for
  `socket_connect` and `ping`

## Rule files
- [~] `include other.rules` and a rules directory read in order — partly: include PATH reads a file or every *.rules in a folder, with the hosted file calls
- [x] Rule expiry (`allow in tcp 9000 until 18:00`) for temporary openings
- [~] A converter from and to iptables/nftables or pf syntax for the
  subset both have — partly: murmansk_iptables and murmansk_from_iptables for the subset both have; no nftables or pf
- [x] A longer drop history than the last 16, sized by a setting
- Hosted mode: apply the same rules to USSR sockets on Windows and
  Linux (a per-program firewall for Ukraine servers)
