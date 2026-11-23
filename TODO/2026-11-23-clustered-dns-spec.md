# Manannan as a clustered authoritative DNS server: the specification

Status: third draft, after a critique and the owner's answers of 2026-11-23
(section 15).
Nothing here is built. Facts about sibling projects come from their
documents and public headers as of 2026-11-23, and are unverified until each
project's session confirms them (section 14).

## 1. What Manannan becomes

Manannan becomes a clustered, highly available, authoritative DNS server on
the group's stack. Dannan provides the cluster, its networking and the
common services, and Dagda stores everything. Manannan owns DNS outright:
the wire format, the UDP and TCP listeners on port 53, the query engine,
the zones, DNSSEC, and the admin API and React dashboard.

The model:

- A domain's registrar (Route 53, GoDaddy and so on) holds the domain's
  delegation: its NS records and, where the name servers are inside the
  domain, their glue addresses.
- The cluster's nodes sit on consumer internet connections, at public
  addresses the ISP may change at any time. Manannan's existing function,
  through its plugin system, keeps the delegation at the registrar pointed
  at the nodes' actual addresses.
- An external client's query is answered by whichever cluster node it
  reaches. The usual cluster rules apply: no special nodes, every node able
  to do every job, and highly available. One node is a cluster of one.

It is written so that cloud-style DNS features (round-robin and weighted
sets, health-checked failover) can be added in later versions without
changing the data model (section 10).

## 2. Requirements

### 2.1 What exists today must keep working

Everything Manannan 0.4.0 does is kept, with the same behaviour:

- R1. Discover the public IPv4 address through a source plugin
  (amazon_check_ip) on a cadence.
- R2. Keep any number of records in any number of upstream registries
  (aws_route53: any number of zones and accounts, per-record credentials and
  TTL) at that address, each record reconciled independently.
- R3. The plugin interface: source and registry plugins as shared objects,
  discovered by scan, with a versioned ABI (`plugin_abi_version`). Existing
  plugins keep working, or the ABI bump says why and the bundled plugins
  move with it.
- R4. The retry discipline: back-off doubling to 64 cadences, parking at 8
  failures in a row, waking on a new value, and an operator retry (SIGHUP
  today, and later also the API).
- R5. Secrets are never logged and are held in locked, cleansed memory.
- R6. A node that only does R1 and R2 (no zones served) is an ordinary
  deployment: a cluster of one with no zones.
- R7. An upgrade from 0.4.x migrates the existing configuration and secrets
  without loss (section 12).

What changes is where the configuration lives. Records, registries,
credentials, cadence and plugin settings move from config.yaml and
secrets.yaml into Dagda, and are edited through the API and the dashboard
(section 7.3). The node's own YAML keeps only what a node needs before it
can reach the cluster (section 9.4).

### 2.2 New

- N1. Authoritative DNS over UDP and TCP on port 53, served by Manannan's
  own server on every node with a public address, for any number of zones.
- N2. The common record types (section 9.2). More are added later as they
  are needed.
- N3. DNSSEC from the first serving version: signed zones, automatic key
  management and rollover, and authenticated denial (section 6).
- N4. Zones and records stored in Dagda and replicated to every node, so any
  node answers from its own copy, and keeps answering while it is cut off.
- N5. Node-bound records: a record whose values are the current public
  addresses of cluster nodes, kept current automatically (section 8). The
  name-server hosts are the main use.
- N6. Delegation kept current at the registrar: the NS host records and
  glue the registrar holds follow the nodes' addresses, through registry and
  registrar plugins (section 7).
- N7. An HTTP admin API (OpenAPI) and a React dashboard on Route 53's model
  in the group's design language, with sign-in through an authentication
  plugin (Lugh first) and a local break-glass administrator (section 11).
- N8. A data model that already carries routing policies and health-check
  references (section 10).
- N9. IPv6: discovering each node's public IPv6 address, and AAAA records,
  both served (N1) and upstream (R2). This absorbs the AAAA work once
  planned for 0.5.0.
- N10. Debian packages for trixie on arm64 and x86-64.

### 2.3 Not in scope for the first versions

- Recursion on public addresses. The public servers are authoritative only
  and refuse recursive queries. A resolver for internal clients, forwarding
  or fully recursive, is an opt-in feature on internal addresses only
  (section 8.4).
- Zone transfer to or from servers outside the cluster (AXFR, IXFR, NOTIFY).
  Replication inside the cluster is Dagda's. Outbound AXFR to external
  secondaries, with TSIG, is a later option.
- Dynamic update from the public internet. Signed dynamic update is an
  opt-in feature for zones the operator opens to it (section 8.4);
  otherwise changes come through the API.

## 3. Delegation and the name-server address problem

A zone's NS records name hosts, and resolvers must find those hosts'
addresses. Here the hosts change address without notice. The name-server
names stay fixed, and their addresses follow the nodes.

It is one model: the registrar holds the domain's name servers, and
Manannan's plugins keep their addresses current. The address can be held at
the registrar in two forms, and a zone may use either or mix them: a host
record with the IP written directly (3.1), or a name whose address record
lives in a hosted zone that Manannan updates (3.2). Neither is a hard
constraint, and a zone may also be delegated elsewhere (3.3).

A mass address change is unlikely for nodes on independent connections, and
likely for nodes that share an ISP or a site. Name-server nodes should be
spread across providers and places. The dashboard warns when every name
server of a zone is a direct-IP host record on one network.

### 3.1 Name servers in a zone the cluster serves (in-domain, with glue)

For example `example.org` is served by `ns1.example.org` to
`ns4.example.org`. The registrar holds the NS records and the glue
addresses. A registrar plugin keeps the glue at the nodes' current addresses
(section 7.2), and the cluster serves the same names and addresses in the
zone itself.

The cost, stated for the operator: glue lives in the parent zone (the
TLD), at the TLD's TTL, which is commonly two days. A resolver that cached
the old glue keeps trying the old address of a changed node until it
expires. It then times out on that server and tries another, as the DNS
does by design. Several name servers on independent connections keep the
domain answering, but an address change costs resolvers a retry until the
cached glue expires.

### 3.2 Name servers in another zone with a short TTL (out-of-domain)

For example `example.org` is served by `ns1.dyn.example.net` to
`ns4.dyn.example.net`, where `dyn.example.net` is hosted at a provider such
as Route 53, or by the cluster itself. The parent needs no glue. The name
server hosts' records carry a short TTL (60 s), kept current by a registry
plugin (exactly today's function) or served directly when the cluster hosts
that zone. An address change reaches resolvers within that TTL. This is the
recommended arrangement for addresses that change often.

### 3.3 Delegation elsewhere

A zone the cluster owns may be delegated to another set of name servers
altogether. Manannan then holds the zone's data and keeps the delegation
current, but does not answer for it. The cluster may also delegate a child
of one of its zones to itself or to anyone else, as ordinary NS records with
glue in the parent zone.

## 4. Architecture

### 4.1 One daemon, composed on the stack

`manannan` is one binary and one composition root, like the other hosted
systems:

- the io context and `dannan::Node`, joined to the cluster by its seeds;
- a Dagda context over the node, for Manannan's domains (section 9.1);
- the DNS server: the UDP and TCP listeners, the query engine, and the
  in-memory snapshot of the signed zones (section 5);
- the signer: DNSSEC keys and signatures (section 6);
- the address service: discovery, and node-bound records (section 8);
- the upstream publisher: today's plugins and reconciliation loop, now also
  keeping delegations current (section 7);
- the admin HTTP API and the dashboard's static files (section 11).

Its message types register from 512, before the node starts.

### 4.2 What lives where

| Piece | Where |
| --- | --- |
| DNS wire format, UDP and TCP listeners on port 53, query engine, zones, DNSSEC signing, node-bound records, routing policies, health checks, upstream publishing, the plugin ABI and plugins | Manannan |
| Cluster membership, node identity and keys, sessions, placement, agreement, supervision, budgets, time | Dannan (exists) |
| Configuration reader: YAML, secrets.yaml, `<PREFIX>_A_B`, `--a-b` | Dannan (exists); it replaces Manannan's own reader of 0.4.0 |
| Logging, status, readiness | Dannan (exists); Manannan's loggers stay only as the plugins' interface, adapted onto it |
| External address discovery | Dannan (exists, in reachability); offered as a source beside the amazon_check_ip plugin |
| HTTP server, JSON, password credentials | Dannan (planned); the admin API waits for them |
| DNS client (resolving names for a project's own use) | Dannan (planned for 0.13.0); it works with any DNS server and never relies on Manannan. Manannan uses it where it resolves names itself (plugin endpoints, health checks) |
| Shared dashboard shell | Lugh (`@lugh/admin-ui`) |
| Single sign-on | Lugh (`lugh_client` relying party), used by Manannan's Lugh authentication plugin |

The dependency points one way. Manannan is a hosted system on Dannan, and
Dannan depends on Manannan for nothing. Dannan is the fundamental layer:
its plan, and whatever it provides to every cluster, are its own and are
not changed by this specification. Manannan's DNS server is Manannan's own
code, for the zones Manannan holds.

## 5. The DNS server

### 5.1 Listening

- UDP and TCP on port 53, IPv4 and IPv6, on every node with a public
  address. Nodes behind NAT do not serve, and are never in node-bound
  answers.
- Whether a node is public is Dannan's answer. Dannan's reachability
  already classifies each node (public, punchable or relay-only) by
  dial-back, and Manannan uses that classification and adds no test of its
  own.
- Manannan's own listeners run on the daemon's io context. Each completion
  is posted to the executor, as the stack requires, and every connection and
  request has a deadline. Binding port 53 takes `CAP_NET_BIND_SERVICE` and
  nothing more. The unit's hardening is otherwise kept (systemd-analyze OK at
  least).
- Answering never waits on the store, the signer or the publisher (law 2:
  the viewer never waits). The query path takes no locks. It reads an
  immutable snapshot that is swapped in whole when a newer one is built.

### 5.2 Answering

- Authoritative answers with the AA bit, from the snapshot of every zone the
  cluster serves. RD is ignored and RA is never set. A query for a name in no
  zone the cluster serves gets REFUSED.
- RFC 1034 and 1035 behaviour: CNAME chains inside the zone, delegations
  answered with referrals and glue, wildcards (RFC 4592), and NXDOMAIN and
  NODATA with the SOA in the authority section (RFC 2308). Names compare
  case-insensitively.
- EDNS(0) (RFC 6891), with a 1232-byte UDP payload advertised. An answer
  that does not fit is truncated (TC), so the client retries over TCP.
- DNSSEC answers when the DO bit is set: RRSIGs, DNSKEY, and authenticated
  denial (section 6).
- ANY is answered minimally (RFC 8482). DNS cookies (RFC 7873) are answered.
- Every query gets a response, or a deliberate drop under rate limiting,
  never a silent failure.

### 5.3 Exposure and bounds

The nodes sit on consumer connections and answer the whole internet:

- Every input is untrusted. The message parser is bounded (size, label
  count, compression-pointer loops, record counts), answers FORMERR to what
  it does not understand, and has a fuzz target. So do the zone-file and API
  parsers.
- Response rate limiting per client prefix, so a node is no use as an
  amplifier. No recursion, minimal ANY, and cookies.
- Bounded resources everywhere: TCP connections per client and in total,
  idle and query timeouts, queues. A bound smaller than one query is not a
  bound (discipline 5).

### 5.4 Consistency across the cluster

- Every node serves from its own replica, and answers while partitioned. A
  slightly stale answer is better than none. A change reaches every node
  within Dagda's replication time.
- The SOA serial never goes backwards on any node. It is derived from the
  zone's Dagda head (its hybrid logical clock stamp, as a 32-bit serial in
  RFC 1982 arithmetic) and is never edited by hand.
- A node that restarts serves its saved copy of the zones at once, whether
  or not it can reach the cluster, and takes newer data as it arrives. It
  refuses for a zone only when that zone's saved signatures have expired and
  it cannot re-sign. After a full outage every node therefore answers again
  as soon as it is up.

## 6. DNSSEC

DNSSEC is part of the first serving version.

- Signing is done ahead of time, off the query path. Each node signs its own
  snapshot with the zone's keys whenever the zone changes, and re-signs
  before signatures expire. A node-bound record set that changes is
  re-signed on its own. Answering only reads signatures that already exist.
- Algorithm 15 (Ed25519) throughout. Every algorithm in a zone must sign
  every record set, so the zone uses one, and the threshold scheme below
  produces Ed25519 signatures. A resolver that does not know the algorithm
  treats the zone as unsigned, not as broken.
- Authenticated denial by NSEC3 with no extra iterations and no salt
  (RFC 9276), or by NSEC by choice.
- No zone key is on every node. The nodes sit in people's homes, so the keys
  are held in two layers:
  - Each node has its own zone-signing key, made on the node, sealed under
    the node's key, and never sent anywhere. The zone's DNSKEY set lists
    every serving node's key, and each node signs its own snapshot with its
    own. A resolver accepts a signature by any key in the set, so answers
    from different nodes mix freely.
  - The zone's key-signing key, the one the parent's DS names, signs only
    the DNSKEY set. It is a threshold key (FROST, RFC 9591): each node holds
    a share, any t of n nodes (3 of 5 by default; all nodes where there are
    fewer than three) sign together, and no node ever holds the key. Fewer
    than t stolen nodes reveal nothing about it.
- Serving and routine re-signing need only the node itself. Changing the
  DNSKEY set (a node joining, leaving or being revoked, or a rollover) needs
  t nodes together. It waits, visibly, until they are there.
- A lost or stolen node is one action in the dashboard: revoke the node in
  the cluster (Dannan's revocation). Its key leaves the DNSKEY set, its
  share is made useless by re-sharing the threshold key among the remaining
  nodes, and its own upstream credential is flagged for revocation (section
  7.3). No other key changes, and the registrar is not involved.
- Rollover is automatic (RFC 7583 timing): a node's signing key by
  pre-publication, and the key-signing key by double signature. The new DS
  is published to the parent through CDS and CDNSKEY (RFC 7344, RFC 8078),
  and through a registrar plugin where the registrar has an API (section
  7.2). The dashboard shows the DS for registrars that need it entered by
  hand, and holds a rollover until the parent has the new DS.
- Signature validity, refresh and the inception offset are bounded settings
  with safe defaults. A node whose clock is out of bounds (Dannan's clock
  checks) does not sign.
- The threshold scheme (distributed key generation, signing, re-sharing) is
  cryptography no other part of Manannan needs to see inside, and other
  projects may want it. Whether it lives in Dannan beside the other
  primitives is a question for the network project (section 14).

## 7. The upstream publisher (today's function, extended)

### 7.1 Kept as it is

The source and registry plugins, their discovery and ABI, the reconciliation
loop, the back-off and parking, and the Secret type all stay. The plugins
receive their settings from Dagda instead of a YAML block. The settings keep
the same YAML-shaped documents, so a plugin's own reader is unchanged. The
logger a plugin receives stays as it is, adapted onto Dannan's logging.

### 7.2 Registrar plugins

A new plugin kind keeps a domain's delegation current at its registrar: the
name-server names, the glue addresses for in-domain name servers, and, where
the registrar's API allows, the DS records. Three ship in 0.5.0: Route 53
Domains; GoDaddy, whose API writes a host record's IP directly; and a
generic one that sends signed dynamic updates (RFC 2136 with TSIG) to any
provider or server that accepts them. Others follow as plugins.
A registrar plugin reconciles exactly as a registry plugin does, with the
same back-off, parking and secrets.

The ABI moves to 3 once, for: AAAA, where a registry says which address
families it writes and the source reports both; the registrar kind; the
authentication kind (section 11.4); and a settings schema per plugin, so the
dashboard can render a form for any plugin.

### 7.3 Configuration in Dagda, and no special publishing node

- Upstream targets (a registry or registrar plugin, its settings, and what
  it keeps) live in Manannan's upstream domain. Credentials are sealed
  secrets that the API never returns, only replaces.
- What every target should hold is derived from the store and the node
  addresses (discipline 1: re-derive, don't assert).
- Each node publishes its own address and nothing else, as 0.4 does today.
  A record bound to a node (section 8) is written upstream by that node, so
  an address is only ever published by the node that observed it, and a
  network split cannot make two nodes write the same record.
- A registrar's delegation is one object per domain (its name servers, glue
  and DS), so it has a single writer at a time: the holder of a fenced lease
  for that domain. Without the lease nothing is written, and a writer whose
  lease has lapsed is refused by the fence.
- Credentials cannot be split as keys can, so each node has its own, with
  the least privilege: a credential that may change only that node's own
  records. A stolen node's credential is revoked alone. The documentation
  gives the policy for each provider. A provider that cannot scope a
  credential that finely is flagged as such in the dashboard.
- A registrar credential is sealed so that only the node holding the
  domain's writer lease can open it, and is limited to delegation updates
  of the named domains, never transfers or contact changes.

## 8. Node addresses and node-bound records

### 8.1 Addresses

Each node discovers its public IPv4 and IPv6 addresses on a cadence. The
source is a source plugin (amazon_check_ip today) or Dannan's own
external-address discovery, chosen per node. The node writes them, with the
time it observed them, to its record in Manannan's nodes domain.

### 8.2 Node-bound record sets

- A record set (A or AAAA) may be node-bound: its values are the current
  addresses of the nodes it is bound to.
- It is bound either to named nodes or to slots. A slot is filled by
  Dannan's placement from the live public nodes, so when a node leaves, its
  slot moves to another and nothing needs editing. The name-server names
  (`ns1` to `ns4`) are bound to four slots by default, and the registrar's
  glue follows them (section 7). No node is special.
- A node whose address is unknown, or older than a staleness bound, or
  which the cluster sees as down, leaves the record set until it reports
  again. If every node in a set has left, the last known values are served
  rather than an empty answer (law 4).

### 8.3 The node zone

A cluster can serve a zone generated from its roster, for clients that find
nodes by name: A and AAAA for the live public nodes, with SRV for advertised
services where wanted, all node-bound with a short TTL. It is an optional
Manannan zone like any other, and nothing in Dannan relies on it.

### 8.4 Optional features: nothing appears unless the operator asks

No project depends on Manannan for DNS, and none should (the owner,
2026-11-23): the group's projects resolve names with the system's resolver.
The features below are Manannan's own, for operators who want them. Each is
off until an operator configures it, and the existence of Dannan's roster
never produces a record by itself.

- Service record sets. An operator may define, in a zone, a name that
  answers with the nodes currently offering a service: the roster advert
  that marks a member, where the host and ports come from, and whether
  membership follows Dannan's health advert (ready and degraded in, draining
  and not-ready out, the not-ready answered if nothing is healthy). A and
  AAAA carry the hosts, SRV the ports, and each member may have its own
  name. Manannan only reads Dannan's roster for this.
- Self-registration. An operator may open a zone, or a subtree of it, to
  signed dynamic updates (RFC 2136 with TSIG), with a key per zone or
  subtree. Records registered this way carry a lifetime and expire unless
  renewed, so a registrant that dies leaves nothing behind.
- Views. An operator may define views of a zone, each selected by the
  network a query arrives from, so that one name answers differently per
  network (overlapping private address spaces, for example).
- Visibility per record set. Each record set is public or internal. An
  internal record set is answered only to queries arriving from networks the
  operator has named as internal, and is treated as absent to everyone
  else. In a signed zone the denial chain is built for the public view, so
  internal names are not disclosed by it.
- A resolver for internal clients. An operator may give Manannan a
  listener on an internal address that answers the cluster's zones and
  resolves every other name. It is configurable: forwarding to resolvers the
  operator names, or full recursion from the root servers, with a cache
  bounded in size and by TTL, and DNSSEC validation of what it resolves. It
  is never offered on a public address, so the public servers stay
  authoritative only and cannot be used as open resolvers. The recursive
  resolver is a second parser of untrusted answers, with its own bounds
  (referral depth, query count per resolution, time) and its own fuzz
  target.

## 9. Data model

### 9.1 Domains in Dagda

| Domain | Holds | Shape |
| --- | --- | --- |
| `manannan/zones` | zones, record sets, DNSSEC settings | a head per zone, on every node |
| `manannan/nodes` | each node's addresses, observation times | register map keyed by node id |
| `manannan/upstream` | upstream targets: registries and registrars | head |
| `manannan/secrets` | upstream credentials, each sealed to the one node that uses it; no DNSSEC private key (node keys stay on their node, the threshold key exists only as shares) | sealed per node |
| `manannan/admin` | admin users (password hashes), API settings, the change log | head |

Writes are availability-first. Any node accepts a change, and concurrent
changes merge. Manannan supplies the merge: per record set, last writer by
HLC stamp, with deletions as tombstones. The record set is the unit of
change, so two administrators editing different records never conflict.
A change batch is atomic on the node that accepts it. Across a merge it is
not: the later change wins per record set. Two valid edits can merge into
an invalid zone, so the merge ends with a fixed, deterministic repair that
every node applies alike (a CNAME loses to other data at its name unless it
is the later change; apex NS and SOA are never removed; a dangling
node binding is dropped). What the repair dropped is recorded in the change
log and shown in the dashboard. The changes that must be unique (creating a
zone, minting a key) go through Dannan's agreement.

### 9.2 Zones and record sets

- Zone: name, delegation arrangement (section 3), NS names and how they are
  bound, SOA settings (contact, refresh, retry, expire, negative TTL),
  default TTL, DNSSEC settings, and the serial derived from the head.
- Record set: owner name, type, TTL, values or a node binding, and a routing
  policy (section 10), which is simple by default.
- Common types, fully supported and validated: SOA, NS, A, AAAA, CNAME, MX,
  TXT (SPF and DKIM included), SRV, CAA, PTR, and DS for delegations, along
  with the DNSSEC types the signer produces (DNSKEY, RRSIG, NSEC, NSEC3,
  NSEC3PARAM, CDS, CDNSKEY). More types are added later as needed.
- Validation on write: CNAME alone at its name, NS and SOA at the apex,
  in-zone targets that exist, and sizes within limits.
- Import and export in RFC 1035 master-file format.

### 9.3 Changes

Every change through the API records who made it and when, in the admin
domain, compacted out of the hot path (discipline 4).

### 9.4 The node configuration that stays in YAML

The node's config.yaml keeps only what it needs before it reaches the
cluster: identity and key paths, cluster seeds, listen addresses (DNS and
admin HTTP), the data directory, the dashboard directory and logging. Dannan's
configuration reader reads it, with secrets.yaml and the MANANNAN_ and
`--flag` overrides, as today.

## 10. Cloud-style features, designed in now

Every record set carries a routing policy from the first version, but only
`simple` is served at first. Later policies need no change to the stored
format:

- multivalue: every healthy value, in rotating order, up to a limit (the
  round-robin of a set);
- weighted: values by weight, for gradual moves;
- failover: a primary set and a secondary, by health;
- later, latency or location, which needs data the cluster does not yet
  have.

Health checks are separate objects that record sets refer to: HTTP, HTTPS
or TCP probes with an interval, a threshold and an expected result. Several
nodes probe each target, and a value counts as healthy only by a quorum of
observers. Results go to Dagda, so every node answers alike. A set with no
healthy values answers with all of them rather than none (law 4). The
variants of such a set are too many to sign ahead of time, so these record
sets, and only these, are signed as they are answered, with the result
cached. That design is settled with the feature, not now.

## 11. Admin API and dashboard

### 11.1 The model: Route 53's, in the group's design language

The API and the dashboard follow Route 53's model, so anyone who has used it
knows their way around. The look is the group's: the shared shell
(`@lugh/admin-ui`) and its styling, not Amazon's.

- Hosted zones: a list of zones, each with its record count, name servers,
  delegation arrangement and DNSSEC status. Creating a zone sets up its SOA,
  its NS set and, by default, its signing.
- Record sets: each zone shows a table of its record sets (name, type,
  routing policy, TTL, values or node binding, health check), filtered and
  searched. A record set is created and edited in a form for its type, with
  a "quick create" for the common case.
- Changes: edits are submitted as a change batch of CREATE, UPSERT and
  DELETE actions on record sets, applied whole or not at all, as Route 53's
  ChangeResourceRecordSets does. Each batch gets an id, and its status is
  PENDING until every live serving node has it and INSYNC after that (from
  Dagda's replication evidence). A node that is down does not hold a batch
  at PENDING; it is listed as behind. A batch overridden in a merge is shown
  as superseded (section 9.1). The API exposes the same, so scripts written against
  Route 53's model port easily, but it is Manannan's own JSON API, not a
  copy of AWS's wire format.
- Routing policies and health checks: chosen per record set, with health
  checks a separate list that record sets refer to (section 10), as in Route
  53. Only "simple" is offered until the later version.
- DNSSEC signing: per zone, with keys, the DS to give the parent, and
  rollover status, laid out like Route 53's DNSSEC signing page.
- Beyond Route 53's model, for what is Manannan's own: nodes (addresses, when
  they were seen, which slots they fill), upstream targets and registrars
  (each record or delegation kept, its last result, retries and parked
  items, with a retry action), plugins and their settings forms, the change
  log, and cluster status.

### 11.2 The API

A REST API over HTTP with JSON, following the group's conventions as the
directory project's management API practises them, so every project's API
reads the same:

- Under `/api/v1/`, on Dannan's HTTP server. Resources are nouns with the
  standard methods: `GET` to read or list, `POST` to create, `PUT` or
  `PATCH` to change, `DELETE` to remove. For example
  `/api/v1/zones`, `/api/v1/zones/{zone}`,
  `/api/v1/zones/{zone}/recordsets`, `/api/v1/zones/{zone}/changes`,
  `/api/v1/zones/{zone}/dnssec`, `/api/v1/nodes`, `/api/v1/upstreams`,
  `/api/v1/registrars`, `/api/v1/plugins`, `/api/v1/healthchecks` (later),
  `/api/v1/audit`, `/api/v1/status`.
- A change batch is a resource: `POST /api/v1/zones/{zone}/changes` with its
  CREATE, UPSERT and DELETE actions returns the batch with its id and
  status, and `GET` on it shows PENDING, INSYNC or superseded (11.1).
- Standard status codes. Failures carry `{"error":{"code","message"}}`. A
  field a route does not know is refused, not ignored.
- ETags for optimistic concurrency: a change or removal needs `If-Match`,
  `428` without it and `412` when it is stale.
- Lists are paged with bounded sizes, and filtered by query parameters.
- Callers are the dashboard, by its session cookie with a CSRF token, and
  automation, by bearer tokens the authentication plugin verifies (Lugh's
  access tokens). Every route declares the role it needs in the router.
- Described by an OpenAPI document that a test checks against the route
  table.
- Every node serves it, and a change made on one reaches the others through
  the store. Credentials are write-only: replaced, never returned.

The nodes are on the public internet, so the admin interface is private by
default: it listens on loopback and the local network only, always over
HTTPS, until the operator opens it. Remote administration goes through a
VPN or a port the operator has chosen to open.

### 11.3 The dashboard

A React single-page application on the group's shared shell, built with the
group's toolchain. It is installed to `/usr/share/manannan/dashboard` and
served by the daemon with the SPA fallback, and its directory is
configurable.

### 11.4 Authentication through a plugin

Sign-in goes through an authentication plugin, a new kind of Manannan plugin
beside sources, registries and registrars (section 7.2).

- The first, and the one the group uses, is the Lugh plugin. It signs users
  in through Lugh's single sign-on (OpenID Connect, by Lugh's relying-party
  client), and maps Lugh groups to Manannan's three roles: administrator
  (everything), editor (the records of named zones) and read-only (named
  zones). A zone can be handed to someone without giving them the cluster.
- The plugin interface is small: start a sign-in, finish it, and give back
  an identity with its roles. Other identity providers can follow as
  plugins.
- Sessions are the daemon's own: same-origin cookies with CSRF protection,
  and no token held in the browser.
- So that a cluster can be set up, and recovered without Lugh, there is a
  local break-glass administrator: username and password, hashed with
  Dannan's credential helper. It is off once an authentication plugin is
  configured, unless the operator enables it.
- Every change records the identity that made it (section 9.3).

## 12. Migration from 0.4.x

- An upgraded node's first start finds the 0.4 config.yaml and secrets.yaml.
  It writes their upstream settings, records and credentials into Dagda (a
  cluster of one, R6), rewrites config.yaml to the node-only settings, and
  keeps the originals beside it. `manannan migrate` gains this step.
- The node then behaves as before: the same records, kept at the same
  address on the same cadence, with no zones served. Each 0.4 record becomes
  a record bound to that node.
- Serving zones is a separate, explicit step afterwards, through the
  dashboard.
- Joining existing nodes into one cluster: one node founds the cluster from
  its 0.4 files, as above. Each further node joins empty, and
  `manannan migrate --into-cluster` imports its 0.4 records and credentials
  into the cluster's store, each record bound to the node it came from. A
  record that clashes with one already there is reported and not imported
  until the operator chooses.
- The upstream configuration can be exported back to YAML for inspection.

## 13. One release, and how it is built and proved

The next version, 0.5.0, does everything in this document except the
cloud-style features of section 10, which follow in a later version on the
same data model. There are no stepped releases in between. 0.4.x stays the
released line, with fixes only, until 0.5.0 ships.

The work lands on `develop` as feature branches in this order, each keeping
R1 to R7 working (the existing tests and the 0.4 upgrade path prove it):

1. On the stack: composed on Dannan, upstream settings in Dagda, the 0.4
   migration, AAAA, packages for arm64 and x86-64.
2. The DNS server: wire format, listeners, query engine, zones in Dagda,
   node-bound records and slots, the node zone, rate limiting.
3. DNSSEC: signing, keys, rollover, the compromise procedure.
4. Registrar plugins, Route 53 Domains first.
5. The API, the dashboard, the authentication plugin kind and the Lugh
   plugin.

0.5.0 cannot ship before the sibling work it stands on exists (section 14):
Dannan's HTTP server and credential helper, and Dagda's register maps and
leases. That is the release's main schedule risk.

Proof, beyond the unit tests:

- Tests are written from the RFCs, clause by clause, with each test file
  naming the clauses it proves. Fuzz targets cover the message parser, the
  zone-file parser and the API's inputs.
- A differential gate, run by hand before release: the same zones loaded
  into an established authoritative server, the same queries sent to both,
  and the answers compared.
- A DNSSEC gate, run by hand: an independent validating resolver resolves
  and validates every name in a test zone served by a real cluster, through
  a key rollover.
- A cluster gate on real nodes: a split, a full power cut, an address
  change on one node and on all, and a revoked node.

Rollout: the two production nodes join one cluster at 0.5.0. They keep
their present records through the migration first, and become name servers
only after the gates above pass on them.

## 14. What Manannan will ask of its siblings

Through the coordinating session, as the group's rules require. Each request
gives what is used, the signatures needed, the step it blocks and the test
that proves it done.

- The network project:
  - Each node's reachability classification, to know which nodes are
    public.
  - Placement over live nodes, for filling name-server slots.
  - Agreement for creating zones and minting keys.
  - External address discovery usable as an address source.
  - Clock bounds for signing.
  - The roster's adverts and the health advert, read only where an
    operator has defined a service record set (section 8.4).
  - Whether the threshold scheme (FROST: key generation, signing,
    re-sharing) lives with its other cryptographic primitives.
  - Revocation of a node, as the trigger for removing its keys.
  - The HTTP server, JSON and the password credential helper.
- The storage project:
  - Register maps, and head-backed leases with a fence, for the registrar
    writer (section 7.3).
  - A head with a merge Manannan supplies, per record set.
  - Sealed secrets readable by the cluster's nodes.
  - Watch, for change notification.
  - Secrets sealed to one named node.
- The directory project: the relying-party client, which Manannan's Lugh
  authentication plugin uses, and a way to read a user's groups for role
  mapping; the shared UI shell at a tagged version, with whatever a
  Route 53-style table-and-form console needs from it.

## 15. The owner's answers (2026-11-23) and what is still open

Answered:

1. DNS is Manannan's: it owns the UDP and TCP listeners for DNS and the DNS
   server itself. Dannan is the fundamental layer and depends on Manannan
   in no way (section 4.2). Reachability is Dannan's, and Manannan adds no
   test of its own (section 5.1).
2. Name servers outside the served zone are supported and recommended for
   addresses that change often, but are not a constraint. A zone may also be
   delegated to the cluster in-domain with glue, or to another set of name
   servers (section 3).
3. Development runs as minor versions from 0.5.0.
4. The common record types first; more later as needed.
5. DNSSEC from the first serving version.
6. Packages for arm64 and x86-64.
7. The model: the registrar holds the domain's name-server records, and
   Manannan's existing plugin system keeps them at the nodes' real addresses.
   Any cluster node answers a client. No special nodes; highly available.
8. The interface follows Route 53's model, with the group's styling and
   design language (section 11.1).
9. Authentication goes through a Lugh plugin: a Manannan authentication
   plugin that signs in through Lugh (section 11.4).
10. Delegation is one model. The registrar's name server may be a name in a
    hosted zone that Manannan updates, or a host record with the IP written
    directly; all nodes changing at once is unlikely (section 3).
11. Each node publishes its own address (section 7.3).
12. No stepped versions: 0.5.0 does everything except the cloud-style
    features (section 13).
13. The two production nodes join one cluster.
14. The zone data lives in Dagda, with the rest of the configuration.
15. A restarted node serves its saved copy at once (section 5.4).
16. Edits merge, with a repair rule (section 9.1).

17. Reachability is Dannan's; Manannan adds no test (section 5.1).
18. DNSSEC keys: Ed25519, a signing key per node that never leaves it, and
    a FROST threshold key-signing key no node holds (section 6).
19. Upstream credentials: one per node, least privilege (section 7.3).
20. Registrar plugins in 0.5.0: Route 53 Domains, GoDaddy and generic
    RFC 2136 (section 7.2).
21. A local administrator for setup and recovery, off once an
    authentication plugin is configured (section 11.4).
22. The admin interface is private by default (section 11.2).
23. Three roles, scoped per zone (section 11.4).
24. No project depends on Manannan for DNS; the group's projects resolve
    names with the system's resolver. The needs five projects sent on
    2026-11-23 were withdrawn the same day.
25. Service record sets from the roster, self-registration by signed dynamic
    update with expiring records, views per network and forwarding for
    internal clients are all in 0.5.0, each off until the operator
    configures it. No record appears merely because the roster exists
    (section 8.4).
26. Public or internal is decided per record set (section 8.4).
27. The threshold scheme is asked of Dannan (section 14).

28. A resolver for internal clients, forwarding or fully recursive as
    configured, off by default and never on a public address (section 8.4).
29. If Dannan cannot take the threshold scheme soon, Manannan writes it and
    it moves down later.
30. The production nodes: one founds the cluster from its 0.4 files, and
    the other joins and has its 0.4 records imported (section 12).
31. The plugin ABI is extended to version 3, keeping the present interface.

32. The API follows the group's standards: REST over HTTP with JSON, as the
    directory project's API does (section 11.2).

Nothing is open.
