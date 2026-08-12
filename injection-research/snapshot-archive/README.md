# Snapshot archive

Pipeline HTTP inputs and the remote-payload sidecar are stored as
`sha256/<digest>`. Objects are immutable and are verified against
`manifest.json` before use.

Set `MATCHERTEXT_SNAPSHOT_ARCHIVE` to this directory or to an HTTP mirror that
serves the same `sha256/<digest>` layout.
