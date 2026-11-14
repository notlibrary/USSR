# Murmansk — refinement ideas

**Today:** the firewall of Moscow's network stack (~490 lines). Rules are
read in order: `allow|deny in|out [tcp|udp|icmp|any] [port|lo-hi]
[from|to addr/bits]`, with a policy per direction. Established flows
bypass the rules. It counts hits, keeps the last 16 drops, and has the
`fw(_):` shell command and `/etc/murmansk.rules`.

## Rule language
- Port lists (`tcp 80,443,8000-8099`) and address lists
- Named sets: `set web = 80,443`, `set lan = 10.0.2.0/24, 192.168.0.0/16`,
  used as `$web` and `$lan`
- Both ends in one rule: `from X to Y`, and source ports
- `reject` next to `deny`: answer with an RST or ICMP unreachable in
  place of a silent drop
- `log` as a rule option (`allow log in tcp 22`) to log matches as well
  as drops
- Rate limiting: `allow in tcp 8443 limit 20/s burst 40`, and a
  per-source connection limit
- Time-based rules (`during 08:00-18:00`)
- Interface names, once Moscow has more than one interface
- ICMP types (`icmp echo-request`)
- TCP flags matching
- Comments kept with each rule and shown in the report
- IPv6 addresses and prefixes, once the stack has IPv6

## Rule management
- `fw(_): "insert N rule"` and `fw(_): "move A B"`
- Named rules: `fw(_): "delete web-in"`
- A dry-run check: `fw(_): "test in tcp 10.0.2.15 8080"` prints which
  rule decides
- Atomic replace: load a whole new rule file, roll back if it has errors
- A timed apply: "keep for 60 s unless confirmed", so a remote session
  cannot lock itself out
- Reset of counters without reloading
- A rule-file syntax checker for use before `save`

## State and protection
- An explicit connection table view: `fw(_): "conns"`
- Timeouts per protocol for UDP pseudo-flows
- SYN flood protection (SYN cookies or a half-open limit)
- Port-scan detection: many ports from one source leads to a temporary
  block
- Fail2ban-style dynamic blocks driven by Novgorod or Ukraine (too many
  4xx from one IP)
- A blocklist file of addresses or prefixes, loaded quickly with a
  prefix trie

## NAT and forwarding
- Port forwarding (`forward tcp 80 to 10.0.2.20:8080`)
- Masquerading/NAT, if Moscow ever routes between interfaces

## Observability
- A persistent drop log to `/var/log/murmansk.log` with rotation
- Per-rule byte counters as well as packet hits
- Statistics by top sources and top ports
- A Kremlin panel indicator and a Petersburg rules editor window

## Performance
- Compile rules to a decision structure (per-protocol and port buckets)
  in place of a linear scan
- Benchmarks for rule sets of 1 000+ rules

## Tests
- Property tests: a rule set and its `murmansk_text` reloaded decide
  the same
- A fuzz test for rule parsing
- An integration test inside Moscow with a QEMU guest pinging and
  connecting

## Application awareness
- Rules by program: `allow out tcp 443 for lenin`, using the process
  that opened the socket
- Ask mode on a desktop: an unknown outgoing connection raises a
  Kremlin dialog (allow once, always, deny)
- Profiles: `home`, `public`, `server`, switched with one command
- Rule groups that can be enabled and disabled as a unit
- Service names for ports (`ssh`, `https`) from a small table
- Host names in rules, resolved at load time and refreshed
- Country or ASN blocks from an offline prefix table

## Deeper inspection
- Fragment handling policy and a drop of malformed headers, counted
  apart from rule drops
- Anti-spoofing: drop packets that claim a local or private source on
  the wrong side
- Logging of the first packet of every allowed connection (an audit
  trail), as an option
- A pcap writer for dropped packets (`fw(_): "capture on"`)
- Outgoing rules consulted for UDP sends and DNS, as they are for
  `socket_connect` and `ping`

## Rule files
- `include other.rules` and a rules directory read in order
- Rule expiry (`allow in tcp 9000 until 18:00`) for temporary openings
- A converter from and to iptables/nftables or pf syntax for the
  subset both have
- A longer drop history than the last 16, sized by a setting
- Hosted mode: apply the same rules to USSR sockets on Windows and
  Linux (a per-program firewall for Ukraine servers)
