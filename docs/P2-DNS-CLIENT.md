# P2 DNS client: correlation, CNAME chains, and no socket

`anemos/dns/dns_client.{h,c}` is the resolver-shaped layer on top of the DNS
codec. The caller sends the query bytes and delivers received frames; the
client owns the pending question, the xid, response matching, RCODE/TC
handling and CNAME-chain following. It owns no socket, no timeout and no
server list — those are transport policy and belong above it.

Written from RFC 1035. No Microsoft source.

## API

```c
AnemDnsClientStatus anem_dns_client_init(AnemDnsClient *cl, uint32_t seed);
AnemDnsClientStatus anem_dns_client_query(AnemDnsClient *cl,
                                          const AnemDnsName *name,
                                          uint16_t qtype, uint16_t qclass,
                                          void *out, size_t cap, size_t *out_len);
AnemDnsClientStatus anem_dns_client_receive(AnemDnsClient *cl,
                                            const void *wire, size_t wire_len,
                                            AnemDnsResult *result);
```

One query may be pending. `query` builds a recursion-desired query with a fresh
xid (caller-seeded LCG, same rule as the DHCP client) and stores the question.
`receive` matches one frame and either consumes the transaction or ignores it.

| Status | Meaning | Pending after |
|---|---|---|
| `OK` | address extracted (`family` 4 or 6, `addr`, `ttl`) | cleared |
| `RCODE` | server returned non-zero RCODE; `rcode` holds it | cleared |
| `TRUNCATED` | TC bit set; retry over TCP | cleared |
| `NO_ANSWER` | matching response, no A/AAAA for the name | cleared |
| `CNAME` | chain ended at a different name; `name` holds the target | cleared |
| `LOOP` | CNAME cycle or chain longer than the answer section | cleared |
| `IGNORED` | wrong id, wrong question, or not a response | **kept** |
| `MALFORMED` | the codec rejected the frame; `codec_status` says how | **kept** |
| `BADSTATE` | no pending query, or `query` while one is pending | unchanged |
| `BADARG` | NULL, or a qtype other than A/AAAA | unchanged |
| `NOSPACE` | query buffer too small | unchanged |

Keeping the transaction pending after `IGNORED`/`MALFORMED` matters: a stray
UDP datagram or a truncated frame must not destroy a legitimate outstanding
question. A response that matches is consumed even when it reports an error,
because retrying the same question would get the same answer.

## Matching

A frame is a candidate response only if all of these hold:

- the codec accepts it;
- QR is set;
- the id equals the pending id;
- QDCOUNT is 1;
- the echoed question name equals the stored name **case-insensitively**
  (RFC 1035 §2.3.3), and its type and class match.

Anything else is `IGNORED`. There is no 0x20-style name randomisation; the
comparison is case-insensitive by design, which is what the RFC requires.

## CNAME following

Starting at the question name, the client repeatedly looks for a CNAME whose
owner equals the current name, and moves to its target. When no CNAME remains,
it looks for an A/AAAA of the requested family owned by the current name. The
walk is bounded by the answer count: a cycle therefore returns `LOOP` instead
of hanging. The address record's TTL is reported; a chain that ends in a name
without an address returns `CNAME` with the target, so the caller can issue the
next query itself.

Only A and AAAA are extracted. PTR/MX/SRV callers use the codec directly; the
client rejects other qtypes at `query` time rather than pretending.

## Verification

`anemos/dns/run.sh` — 24 checks for codec + client. The client-specific ones:

1. **Scenario suite, i686 PE under Wine.** Responses are assembled byte by byte
   in the test, independently of the codec's writer. Covered: A, AAAA, CNAME
   chain, CNAME-only, CNAME loop, NXDOMAIN/SERVFAIL, TC, no answer, wrong id,
   wrong question, non-response frames, short malformed frames (and that they
   leave the transaction pending), case-insensitive question echo, second-query
   `BADSTATE`, receive-without-pending `BADSTATE`, short query buffers, and
   every NULL contract.
2. **A case-sensitive comparison must fail.** The negative build disables ASCII
   folding; the uppercase-echo scenario and the corpus both reject it.
3. **Differential corpus against an independent Python model.**
   `dns_client_ref.py` implements matching, RCODE/TC handling and the same
   CNAME walk. It generates **2,040 cases**: 16 hand-authored query/response
   pairs, every truncation prefix of each, and deterministic byte mutations,
   insertions and deletions. Both implementations must agree on the status,
   codec status, RCODE, family, exact address bytes, TTL and final-name bytes
   for every case. The C side runs under ASan and UBSan.
   Coverage: 40 `OK`, 333 `IGNORED`, 1,499 `MALFORMED`, 10 `TRUNCATED`,
   22 `RCODE`, 83 `NO_ANSWER`, 42 `CNAME`, 11 `LOOP`.
4. **Freestanding object + size budget.** `dns_client.c` compiles for
   `i686-windows-msvc`; `.text` is **1,117 bytes**.

Reproduce:

```sh
anemos/dns/run.sh
```

## Limits

- **One pending query.** A resolver that pipelines several questions, or issues
  the next CNAME query before the previous answer arrives, sits above this
  unit.
- **No transport.** Timeouts, retransmission, server selection and TCP fallback
  on TC are the caller's. The status values carry enough information to drive
  them.
- **No DNSSEC, no EDNS(0).** Both are outside the codec too; the client does
  not inspect OPT.
- **A/AAAA only.** Deliberate; the extraction rules for the other types belong
  to their callers.
- **TTL is the address record's.** A chain's CNAME TTLs are not folded into a
  minimum. A resolver that caches needs to decide that policy explicitly.
- **No search domains, no ndots, no hosts file.** Name qualification is policy
  above this layer.

## What it proves and what it does not

It proves response correlation, error mapping, CNAME traversal and address
extraction agree with an independent implementation over 2,040 valid and
mutated inputs, and that irrelevant or malformed traffic cannot destroy a
pending transaction.

It does **not** prove XP's resolver makes the same choices — search lists,
negative caching, EDNS fallback and server rotation are all above this layer.
The XP oracle becomes usable when the socket path exists (P4).
