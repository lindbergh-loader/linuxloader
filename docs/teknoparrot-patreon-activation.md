# TeknoParrot's Patreon activation check

## Latest test: local activation response (2026-11-05)

The later GE-Proton11-10 test changes the conclusions of the original capture
below. Wine's `+winhttp` trace shows this build uses WinHTTP and honors
`http_proxy=http://127.0.0.1:18080`. A regular mitmproxy listener intercepted
the request after its CA was trusted in this prefix.

HTTP 200 with bare `OK` gives `Activation failed, bad reply!`. The following
347-byte body instead gives `[SUBSCRIPTION MODE] Activation key ok!`:

```python
import base64
body = b"OK\n" + base64.b64encode(bytes(256))
```

This was observed both directly under BudgieLoader and through
`TeknoParrotUi.exe --profile=Minecraft.xml`. It passes the initial response
check; it does not establish that later license checks or gameplay succeed.

Run the helper from the repository root:

```sh
python3 tools/teknoparrot-activation.py
# Or keep only the proxy running:
python3 tools/teknoparrot-activation.py --proxy-only
```

The helper defaults to this repository's `wineprefix`, the sibling
`windowsarcadeloader/wine-runners/GE-Proton11-10-x86_64` runner, and
`Minecraft.xml`. On first use it installs mitmproxy in a private virtual
environment if needed. Certificates and logs stay under
`wineprefix/.teknoparrot-activation/`. It imports the CA into that Wine prefix,
starts the endpoint-specific addon `tools/teknoparrot-ok.py`, and supplies
proxy variables to the launched UI. Proxy-only mode requires applications to
be launched with the printed proxy variables. The proxy stops with the helper.

The game currently stops later with a VM-detection error. A local `BANNED`
control reply also produces that error, so it is not unique to the synthetic
success payload. `HideWineExports=Y` did not remove it and was reverted.
The installed loader executable was not patched.

## Original capture (historical findings)

Some TeknoParrot games (the Patreon/subscriber titles, e.g. Minecraft Dungeons
Arcade) refuse to start unless the loader is "activated". The loader is
`BudgieLoader_x64.exe` (`teknoparrot/ElfLdr2/x64/`); it is packed — every real
PE section is empty on disk (`.brr*`, `.teknoGo`) and filled in at runtime, so
none of this is visible as plain strings. The check is **online and
server-side**, not a local validation of the key.

## The flow

1. BudgieLoader reads the subscriber serial from the registry:
   `HKEY_CURRENT_USER\Software\TeknoGods\TeknoParrot`, value `PatreonSerialKey`
   (a REG_SZ like `XXXXX-XXXXX-XXXXX-XXXXX`). In the wine prefix this lives in
   `wineprefix/user.reg`.
2. It opens a **raw TLS socket** (WS2_32 / `SOCK_STREAM` to port 443) straight to
   `teknoparrot.com` — not through WinINET, so `HTTP(S)_PROXY` env vars do not
   catch it. The host resolves to `51.68.254.174`.
3. It sends one plain `GET` to `/vmp/activation.php` with the serial, a key, the
   machine id and a hash as query parameters, and decides based on the reply.

The verdict comes entirely from the server, keyed on the serial. Observed
console lines, by what the server/network did:

| situation                          | BudgieLoader prints                      |
|------------------------------------|------------------------------------------|
| server reachable, serial rejected  | `[SUBSCRIPTION MODE] Activation failed, banned!` |
| no route to the server             | `[SUBSCRIPTION MODE] Activation failed, no internet connection!` |
| reply missing/invalid (we blocked it) | `[SUBSCRIPTION MODE] Activation failed, bad reply!` |

So a banned serial cannot be worked around by changing the wine prefix, the
runner, or the local registry: the ban lives on the server.

## The request

```
GET https://teknoparrot.com/vmp/activation.php?code=<serial>&key=<base64>&hwid=<id>&hash=<base64> HTTP/1.1
User-Agent: Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.2)
Host: teknoparrot.com
Connection: Keep-Alive
Pragma: no-cache
Cache-Control: no-cache
```

Empty body — it is a plain `GET`. Parameters:

| param  | example (this machine)            | meaning |
|--------|-----------------------------------|---------|
| `code` | `XXXXX-XXXXX-XXXXX-XXXXX`          | the Patreon serial, verbatim from `PatreonSerialKey` |
| `key`  | `<key>` (base64)            | a short opaque blob |
| `hwid` | `<hwid>`                 | per-machine hardware id |
| `hash` | `<hash>` (base64) | a digest/HMAC binding the other fields to this machine |

`hwid` and `hash` are specific to the machine and serial; `code` is the stored
serial. Only the request shape is general.

Notably, BudgieLoader does **not** validate or pin the server certificate — a
MITM cert signed by an untrusted CA was accepted and the request went through
unchanged.

## How the request was captured (request only, no response)

Everything ran inside a user+net+mount namespace so it is self-contained and
tears down on exit, and no real root is needed:

```sh
unshare -rnm bash -c '
  ip link set lo up
  # point the host at a local proxy...
  mount --bind hosts.mitm       /etc/hosts        # "127.0.0.1 teknoparrot.com"
  # ...and force /etc/hosts to be consulted: the default Arch/CachyOS nsswitch
  # puts "resolve" (systemd-resolved) before "files", which ignores /etc/hosts.
  printf "hosts: files dns\n" > nss.tmp
  mount --bind nss.tmp          /etc/nsswitch.conf

  mitmdump --mode reverse:https://teknoparrot.com:443@443 \
           --set connection_strategy=lazy -s capture.py &
  # lazy: mitmproxy completes the CLIENT handshake from its own CA without ever
  # contacting the real server, so the request is logged and the response is
  # never fetched. The namespace has no outbound route anyway.

  wine BudgieLoader_x64.exe "<game>/gamedump.exec"
'
```

`capture.py` logs the request on the `request` hook and blanks
`flow.response.content` so a reply, if one ever arrived, never surfaces. The
reusable harness is in the session scratchpad: `run-lazy.sh`, `capture.py`,
`hosts.mitm`, `nss.tmp`.

## Why this matters for the port

For a dongle game we instrument the dump and record the HASP encrypt/decrypt
answers (see [dongle-recording.md](dongle-recording.md)). But the game only
reaches the dongle code *after* BudgieLoader activates. With a banned serial it
never gets there, so no answers can be recorded this way. The alternatives are a
serial the server accepts, or answering the dongle's decrypts directly in our
own loader instead of going through TeknoParrot. See
[../docs/hasp-bypass.md](hasp-bypass.md).
