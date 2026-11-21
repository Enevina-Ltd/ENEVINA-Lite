# Midea / OEM models and native Lite coverage

**Sources checked: 21 November 2026.** Lite implements the residential AC
**device type `0xAC` over LAN V2 or V3**, including classic C0/A0 and encapsulated
BB families. The adapter is `midea_lan`. Compatible models use this common path;
there is no retail-name allowlist and no need to add a duplicate driver for
each OEM badge. Discovery, valid current state and reported capabilities select
the available controls. The device type identifies the protocol family; it does
not prove that every model uses all of the implemented command layouts. Known
exceptions and gaps are recorded below.

This catalogue records what the sources actually establish. **An upstream
device report is evidence for the reference integration; Lite's own physical
result is recorded separately.** Model names below are not a promise that every
regional Wi-Fi module, firmware or optional function is identical.

## Named devices found in primary sources

| Brand / model | Source evidence | Current Lite coverage and relevant limitation | Primary source |
| --- | --- | --- | --- |
| **Frezzer Nordic**; exact retail submodel unrecorded | **Lite physical test**, firmware `2026112102`: OFF / HEAT 22 -> 23 -> 22 C, independently queried after each change | Native V3 `0xAC`, short C0. Reported fan/swing/presets unchanged; absent optional fields remain unknown. Other controls were not exercised in this test | [Field receipt](../field/MIDEA_LAN_CONTROL_20261121.md) |
| **Midea Mission Pro 9** | Upstream merged fix explicitly tested reading and setting 16.0 and 16.5 C | Common AC command path. Device-reported temperature range/step govern Lite; module and LAN version must still be discovered | [msmart PR #78](https://github.com/mill1000/midea-msmart/pull/78), [issue #77](https://github.com/mill1000/midea-msmart/issues/77) |
| **Midea Xtreme Save Pro** | User reports working horizontal/vertical blade adjustment; internal code `00000Q11` | Common AC path and optional property controls when verified. The report concerns beep suppression; it is not proof that every optional property is supported | [midea-ac-py #108](https://github.com/mill1000/midea-ac-py/issues/108) |
| **Midea Breezeless+ / Breezeless Plus** | Working blade adjustment reported for `00000Q19` | Common AC path; Breezeless and angle controls depend on reported capabilities and fresh property state. Capacity SKU/module revision not established | [midea-ac-py #108](https://github.com/mill1000/midea-ac-py/issues/108), [#60](https://github.com/mill1000/midea-ac-py/issues/60) |
| **Midea Breezeless E** | Live V3 state/capability exchanges and upstream development of newer property controls | Common AC and selective B0/B1 paths. The thread covers several units; it does not establish one universal model code or complete feature parity | [midea-ac-py #60](https://github.com/mill1000/midea-ac-py/issues/60) |
| **Midea Xtreme Save Blue R290** | User reports working integration and requests missing defrost indication; internal code `00000Q1C` | Common AC path. This report does not prove a defrost sensor or Lite field operation | [Midea AC LAN #749](https://github.com/wuwentao/midea_ac_lan/issues/749) |
| **Midea PortaSplit** | Real V3 devices, subtype `524`, codes `00000Q1D` and `2060008E`; corrected upstream self-clean cycle confirmed | Common AC path; optional self-clean/energy features have their own semantics. These internal codes must not be used as unique retail-name identifiers | [Midea AC LAN #971](https://github.com/wuwentao/midea_ac_lan/issues/971), [AC documentation](https://github.com/wuwentao/midea_ac_lan/blob/089654865c697185d2e487f9550ff685df44fb8d/doc/AC.md) |
| **Unical KMUN 13H** | V3 `0xAC`, code `00000Q1B`, subtype `0`; vertical and horizontal swing physically work in the reference integration | Common AC path. Source reports appliance-message version 3; the discovered version must not be guessed from the retail name | [Midea AC LAN #1010](https://github.com/wuwentao/midea_ac_lan/issues/1010) |
| **Comfee Eco Cool Pro 2.9** | User reports successful cooling/power control through the AC integration | Candidate for the common AC path; the report does not supply a numeric discovery type or LAN version. Verify them at discovery. Do not infer heating capability | [midea-ac-py #157](https://github.com/mill1000/midea-ac-py/issues/157) |
| **Inventor Aria** | V3 `0xAC`, code `00000Q1B`, subtype `44204`; live HA state and app control reported | Common AC path candidate; **LAN display control is not established** and the source reports no effect from display commands. No Lite physical test | [Midea AC LAN #1093](https://github.com/wuwentao/midea_ac_lan/issues/1093) |
| **Unical KMUN 10H** | V3 `0xAC`, code `00000Q1B`, subtype `0`; state reading works but LAN swing command is reported ineffective | **Partial upstream evidence.** Do not claim universal swing control or force a protocol-version override. This unit reports appliance-message version 0 despite sharing identifiers with KMUN 13H | [Midea AC LAN #1010](https://github.com/wuwentao/midea_ac_lan/issues/1010) |
| **Rotenso Nevo N70Xi / UO70Xo**, module **LCAC EU-OSK105** | Exact units/module and AC state exchanges reported; internal code `00000Q1C` | Common AC path candidate. The report concerns missing energy/power entities; it does not independently prove a Lite SET. The old reported negative subtype is not used as a matching rule | [Midea AC LAN #444](https://github.com/wuwentao/midea_ac_lan/issues/444) |

The table contains **12 named rows** with differing evidence, not 12 independently
tested Lite installations. Some entries are air conditioners; heating is offered
only when the actual device supports it. This is a useful source-backed starting
catalogue, not an exhaustive list of all compatible retail products.

## OEM brands named by the reference integration

The pinned [Midea Smart AC README](https://github.com/mill1000/midea-ac-py/blob/42cfe961184a530b38b560d48c26e1268a4e60c2/README.md)
names these **35 OEM brand candidates**:

AirCon, Alpine Home Air, Artel, Beko, Canair, Carrier, Century, Comfee,
Electrolux, Friedrich, Galactic, Goodman, Hualing, Idea, Inventor, Kaisai,
Kenmore, Klimaire, Lennox, LG, Mitsui, Mr. Cool, Neoclima, Olimpia Splendid,
Pioneer, Pridiom, QLIMA, Qzen, Rotenso, Royal Clima, Samsung, Senville,
Thermocore, Toshiba and Trane.

Midea itself and the reference's **Artic King** app are also relevant. The
Frezzer and Unical evidence is listed above. Additional upstream reports mention
[Fisher and Danby](https://github.com/wuwentao/midea_ac_lan/issues/895) and a
[Senville 9k BTU ducted unit](https://github.com/mill1000/midea-msmart/issues/213),
but do not identify exact retail models reliably enough to add invented SKUs.

**The condition for the current Lite adapter is always the actual `0xAC`
V2/V3 interface.** A brand may sell Midea-based AC units, another vendor's AC
units, commercial systems and hydronic pumps. In particular, this list is not
a claim that all Toshiba, LG, Samsung, Carrier or other listed products use
Midea LAN. Unlisted compatible `0xAC` brands remain eligible for the same adapter.

The upstream application names provide a discovery clue, not a compatibility
override: **Artic King, Midea Air, NetHome Plus, SmartHome/MSmartHome, Toshiba AC
NA and 美的美居**. Existing V3 token/key provisioning is separate from subsequent
local control. See the [connection guide](LOCAL_WIFI_CONNECTION_GUIDE.md).

## Model-specific code audit

| Reference identity / behavior | Native Lite handling | Source |
| --- | --- | --- |
| `22013279`: stale C0 temperatures; use new `0x007e` layout | Exact model match already selects the verified new temperature decoder. Current-cycle `0x007e` is needed before control; no arbitrary subtype restriction | [Pinned AC device](https://github.com/midea-lan/midea-local/blob/425bba4a05b74b619d31bf2d8b5a6baf030aa042/midealocal/devices/ac/__init__.py), [PR #572](https://github.com/midea-lan/midea-local/pull/572) |
| `22251759` / subtype `32773`: useful classic C0 temperature | Not given the `22013279` override; generic C0 remains authoritative | [Current fork AC source](https://github.com/wuwentao/midea-lan/blob/e414af70d57cd7fbbac1ddcf50e700077478a28c/midealan/devices/ac/__init__.py) |
| `220F4047` / subtype `8`: different C0 room-temperature encoding and outdoor placeholder | Exact read-decoder correction: indoor byte / 2 plus decimal, without the generic offset; outdoor `0x20` remains unknown. This does **not** establish operating-control compatibility; see the gap below | [Current fork AC source](https://github.com/wuwentao/midea-lan/blob/e414af70d57cd7fbbac1ddcf50e700077478a28c/midealan/devices/ac/__init__.py), [issue #998](https://github.com/wuwentao/midea_ac_lan/issues/998) |
| `23096633` / subtype `1`: BB family | Exact identity rule already selects BB; validated BB responses can also identify the family on another model. BB timer/SN8 readback requirements remain | [Pinned AC device](https://github.com/midea-lan/midea-local/blob/425bba4a05b74b619d31bf2d8b5a6baf030aa042/midealocal/devices/ac/__init__.py) |
| `00000Q16`, `00000Q1B`, `00000Q1F`: energy decoding method 12 | Not a separate basic-control codec. Lite does not expose these upstream energy counters, so no fabricated power/energy values are added | [Pinned climate exceptions](https://github.com/midea-lan/midea-local/blob/425bba4a05b74b619d31bf2d8b5a6baf030aa042/midealocal/const/climate_exceptions.py) |
| `00000Q1B` / `44204` and `00000Q1F` / `44204`: stale A0 display field in the newer fork | Lite does not mark the legacy C0/A0 display bit as an absolute-display observation. Absolute display control requires B1 `0x0017` evidence, so this stale A0 bit does not authorize or confirm it | [Current fork AC source](https://github.com/wuwentao/midea-lan/blob/e414af70d57cd7fbbac1ddcf50e700077478a28c/midealan/devices/ac/__init__.py), [issue #1147](https://github.com/wuwentao/midea_ac_lan/issues/1147) |

An internal eight-character model code is shared by multiple marketed products.
It is suitable for a narrowly evidenced protocol exception, not for inventing
a retail name or guaranteeing every optional feature. The setup result therefore
shows the **reported internal code**, not a guessed model label.

Native implementation: `pump/midea_ac_protocol.*`, `pump/midea_ac_capabilities.*`,
`pump/midea_discovery_info.*` and `firmware/cottage/src/cottage_midea.cpp`.
See [control and capability semantics](../MIDEA_LIBRARY_INTEGRATION.md) for
the exact commands, limits and observed-state requirements.

### Identified control gap within the AC family

**Hualing/Midea internal code `220F4047`, subtype `8`** has no reliable retail
SKU in the examined report. A maintained local adapter reports successful
operating changes using a grouped B0 `0x0001` property with
`[power, mode, round(target_c * 2), fan]`. That adapter also requires a complete,
validated B1 operating-state observation and handles conflicting C0 target
readback. These results are explicitly from the reporter's local adapter,
not an unmodified upstream release.

Lite does **not** implement that grouped operating-property route. The exact
temperature-read correction above is therefore not a claim of full support for
this model. Its physical operating-control compatibility remains unverified;
the report alone also does not prove that every existing classic command fails.
It is kept in the catalogue as an identified gap, not silently included in the
list of models with positive command evidence or blocked by an invented brand
rule. Primary receipts: [grouped command and hardware cycle](https://github.com/wuwentao/midea_ac_lan/issues/998#issuecomment-6036795882),
[temperature and fan follow-up](https://github.com/wuwentao/midea_ac_lan/issues/998#issuecomment-6038171451).

## Protocol boundaries

| Discovered family | Reference implementation | Current native Lite status |
| --- | --- | --- |
| **`0xAC`, LAN V2/V3** | Residential AC / air-to-air heat pumps | Implemented common adapter with the model/feature limits above; the Frezzer short-C0 path has the physical result above |
| **`0xCC`** MDV/commercial AC/VRF; e.g. internal panels `171PNL01`, `171PANEL` | Separate legacy CC and newer FE/TLV formats; different mode/temperature encoding | **Not implemented.** Changing an AC frame header cannot add this support. [CC source](https://github.com/mill1000/midea-msmart/blob/dd0994755bb078b613837038f2d67d97fab133ab/msmart/device/CC/command.py), [CC documentation](https://github.com/wuwentao/midea_ac_lan/blob/089654865c697185d2e487f9550ff685df44fb8d/doc/CC.md) |
| **`0xC3`** heat-pump Wi-Fi controller / hydronic family; internal controllers `171H120F`, `17100007` | Separate hydronic temperatures, zones and water-related commands | **Not implemented.** [C3 documentation](https://github.com/wuwentao/midea_ac_lan/blob/089654865c697185d2e487f9550ff685df44fb8d/doc/C3.md) |
| **`0xCD`** heat-pump water heater | Separate device-family codec | **Not implemented.** [CD documentation](https://github.com/wuwentao/midea_ac_lan/blob/089654865c697185d2e487f9550ff685df44fb8d/doc/CD.md) |
| **`0xCF`** heat pump | Separate device-family codec | **Not implemented.** [CF documentation](https://github.com/wuwentao/midea_ac_lan/blob/089654865c697185d2e487f9550ff685df44fb8d/doc/CF.md) |
| V1, newer API-v2-only devices, other vendor/Tuya/cloud-only protocols | Different access/packet semantics | **Not implemented by this V2/V3 adapter.** A familiar brand or app name is insufficient |

The Python source under `third_party/midea-local/` is a development reference;
it is not run on the ESP32. Its other appliance modules do not create native
Lite support. Known non-AC discovery results are identified as unsupported in
setup; incomplete metadata remains discoverable for the existing state-read
verification instead of being rejected by a retail-name list.

## Reference snapshots

The following repository heads were checked on the date above. Protocol
references already used by Lite remain pinned; this audit did not silently
replace the vendored library or add a cloud dependency.

| Project | Checked commit |
| --- | --- |
| `mill1000/midea-ac-py` | `42cfe961184a530b38b560d48c26e1268a4e60c2` |
| `mill1000/midea-msmart` | `dd0994755bb078b613837038f2d67d97fab133ab` |
| `midea-lan/midea-local` (vendored 15.2.1) | `425bba4a05b74b619d31bf2d8b5a6baf030aa042` |
| `wuwentao/midea_ac_lan` | `089654865c697185d2e487f9550ff685df44fb8d` |
| `wuwentao/midea-lan` (newer HA fork) | `e414af70d57cd7fbbac1ddcf50e700077478a28c` |

## Suomeksi

**Liten nykyinen yhteinen Midea-ajuri ohjaa paikallista V2/V3-yhteyttä käyttäviä
`0xAC`-ilmastointilaitteita ja ilmalämpöpumppuja.** Mallin nimeä ei tarvitse lisätä
erilliseen sallittujen listaan. Yllä on 12 lähteistä tarkistettua malliriviä sekä
35 lähdeprojektin nimeämää OEM-merkkiehdokasta. Frezzer Nordic on testattu
fyysisesti Litellä; muiden kohdalla kerrotaan lähdeprojektin havainto ja rajaukset.
Laitehaku näyttää saatavilla olevan laitetyypin, sisäisen mallikoodin ja alatyypin.
Mallin `220F4047` / `8` lämpötilan lukutapa on huomioitu, mutta sen erillistä
ryhmiteltyä ohjausprotokollaa ei ole toteutettu eikä ohjausta varmennettu Litellä.
`0xCC`- ja vesikiertoiset Midea-perheet tarvitsevat omat toteutuksensa.
