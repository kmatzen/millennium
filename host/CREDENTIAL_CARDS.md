# Credential cards

The reader must only be used with dedicated Millennium credential cards. Do
not swipe payment cards and do not copy payment-card account numbers into the
daemon configuration, logs, stories, tests, or support tickets.

Generate a random 16-digit identifier for each card:

```sh
python3 -c 'import secrets; print(f"{secrets.randbelow(10**16):016d}")'
```

Encode that identifier as the Track 2 account field on a dedicated test card,
then add it to either `card.free_tokens` or `card.admin_tokens` in
`/etc/millennium/daemon.conf`. Keep the mapping between a physical card and its
purpose in the private device inventory, not in this repository.

The daemon redacts credential events and clears their in-memory event buffers
on destruction. The keypad firmware also clears its raw read and transmit
buffers immediately after forwarding a credential.
