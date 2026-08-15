# Security policy

## Supported versions

The current `0.1.x` release line receives security fixes. Pre-1.0 minor releases
may change public APIs; consumers should request an exact version or remain on
the same minor line.

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting for
`izandotai/predictfun-cpp`. Do not include API keys, JWTs, private keys,
mnemonics, signed mutation payloads or funded account details in a public
issue.

The SDK treats credentials, signing and mutation authority as caller-owned.
Reports should state which linkable target is involved and whether the issue
can cross an authority boundary without explicit host authorization.
