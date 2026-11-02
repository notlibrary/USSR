# Murmansk

Murmansk is the firewall of Moscow's network stack
([src/moscow/net.su](../moscow/net.su)): a list of rules, read in order,
the first one that matches a packet deciding, and a policy for each
direction when none does. It is plain USSR (`murmansk.su`); the kernel
includes it and asks it about every packet that starts something.

```
# /etc/murmansk.rules: the first rule that matches decides
policy in deny
policy out allow
allow in icmp
allow in tcp 8080
allow in tcp 8443
deny in tcp 9000-9099 from 10.0.2.0/24
allow in tcp 9000-9099
allow out tcp 443 to 192.168.1.0/24
```

A rule is `allow|deny in|out [tcp|udp|icmp|any] [port|lo-hi]
[from|to a.b.c.d[/bits]]`: `from` the other end for `in`, `to` it for
`out`; a port is the local one coming in and the remote one going out;
what is left out matches anything. `#` starts a comment.

## What it is asked

Murmansk decides about what is new:

- coming in: a SYN to a port (or any TCP segment of no connection the
  machine has), a ping, a datagram;
- going out: `socket_connect` and `ping`.

What belongs to something going on is the network stack's to let
through, and never reaches the rules: the segments of a connection the
machine accepted or opened, the answers to its ping, the DHCP exchange
at boot. A packet Murmansk denies is dropped without an answer, not even
an RST: from outside, a closed port looks like no machine. Drops are
counted, and the last 16 are kept with the rule that dropped them.

## In Moscow

At boot the rules come from `/etc/murmansk.rules` (the install disk
has the rules above: ping and the forum's ports 8080 and 8443); without
the file, nothing comes in and everything goes out.

```
fw:      Murmansk, 3 rules from /etc/murmansk.rules; in deny, out allow
russia> fw(_):
policy in deny, out allow
1. allow in icmp   (0 hits)
2. allow in tcp 8080   (0 hits)
3. allow in tcp 8443   (0 hits)
0 packets dropped
russia> fw(_): "allow in tcp 9000"
russia> fw(_): "delete 4"
russia> fw(_): "policy out deny"
russia> fw(_): "save"
```

`fw(_):` with no argument prints the rules, their hits and the last
drops; with one it adds a rule at the end, or `delete N`, `policy
in|out allow|deny`, `save` (into `/etc/murmansk.rules`) and `load`.
Changes last until the machine stops unless saved; a program that
changes them (inside `run`) changes them until it ends.

## As a library

| Call | Does |
|------|------|
| `murmansk_reset(_):` | no rules; in deny, out allow |
| `murmansk_add(r): text` | a rule or a policy line: 0, or -1 (`murmansk_error`) |
| `murmansk_delete(r): n` | rule n (from 1) goes |
| `murmansk_load_text(n): text` | a file's lines; how many were wrong |
| `murmansk_text(s):` | the rules as a file holds them |
| `murmansk_allows(b): dir proto ip port` | the decision, nothing counted |
| `murmansk_check(b): dir proto ip port` | the decision, counted and logged |
| `murmansk_report(s):` | rules, hits, drops, the last drops |

Tests: `ussr tests/murmansk_test.su` (rules, order, ports, networks,
policies, the file form, the log); `qemu_test.py` boots Moscow, lists
the rules, denies a ping with one, and has a client of the host find a
port no rule opens shut while the forum's port works.
