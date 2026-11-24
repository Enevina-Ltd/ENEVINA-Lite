# Transport and market memory follow-up

## Concrete defects
The firmware still held a 7.5 KiB static legacy plan array although both
plan APIs already used the validated shared MPC record. The optimizer
also allocated and copied an unused PSRAM mirror. The price cache and
temporary price/normalization arrays occupied more scarce internal RAM.
Remove the obsolete mirrors, explicitly allocate the price cache and
forecast scratch in PSRAM, and validate each market slot without a
second full array. The compact codec validates before writing its
destination and no longer creates a full PriceCache stack temporary.
No task stacks, thresholds, watchdogs or control permissions are relaxed.

A large dashboard response could exceed the existing absolute HTTP
response budget. Precompress the exact HTML during the build, negotiate
gzip using the validated request headers, and send the static compressed
bytes through the same bounded writer. Identity clients retain the raw
HTML. Serialized JSON buffers use the ESP32 memory-mapped send_P path
instead of the SDK char-pointer-to-String copy. A single nonoverlapping
periodic UI sequence replaces independent regular timer bursts, and
unselected Midea status is no longer requested periodically. Other
adapter selection and wizard/action-specific reads remain supported.
The MPC chart is loaded when its view is selected. The browser pauses
regular polling while hidden.

## Regional persistence
For FI/EUR preserve the legacy compact price_v2 format; it is only a
Finland cache. A genuine absent legacy stamp or FI/EUR stamp is accepted;
NVS errors and foreign stamps are not. For other zones the existing
regional cache key stores a versioned envelope with area/currency and
the compact payload in a single NVS blob. Ambiguous old split-stamp
regional records are not migrated or treated as another zone; they
are fetched again. Before dropping an incompatible old stamp, remove
any ambiguous old legacy blob. A runtime region change clears RAM and
wakes the sole market reader instead of racing its NVS save. Late
load/fetch publication is fenced by the captured region generation.
Array reads can require the market status's exact region/cache revision;
the controller also checks the saved site's area and currency.

A power loss or NVS write failure may lose a replaceable forecast cache;
it cannot be used under another regional identity by the new reader.
No credentials, saved site settings, OTA records or sensor bindings
are touched.

## Verification scope
Host tests pin gzip quality negotiation including explicit refusals,
byte-exact deterministic payload, regional identity mismatch, bounded
writer and existing optimizer/control regressions. Browser and ESP32
bench tests must distinguish successful loading/forecast refresh from
actual physical pump control. No claim of elimination of all possible
memory peaks or field savings without the longer production test.
