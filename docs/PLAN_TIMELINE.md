# Plan timeline (19.11.2026)

**Status: logic host-tested on real recorded prices, firmware builds, UI driven in
a browser (375 px phone, FI/EN) against `tools/dev_device_simulator.py`. NOT run
on a flashed ESP32-S3; the simulator replays recorded price data and a synthetic
outdoor series.**

## What it shows

`GET /api/timeline` (home LAN) returns up to 192 consecutive 15-minute slots from
the published prices with:

* the **price class** of every slot - cheap / neutral / expensive / unknown -
  computed by `classify_price()` in `cottage/price_bootstrap.hpp`, the very
  function the live controller calls, with the same profile window (shortened
  near the end of the data, at least 8 slots) and threshold;
* **periods** (runs of cheap or expensive slots, at least 30 minutes) with the
  real average price, comparison median and range;
* the controller's **headroom** for the chosen profile (`preheat_c`, `coast_c`,
  owner limits) from the same `apply_profile()` call, and **today's decision**
  (action, setpoint, reason code) from the controller itself.

The plan view draws price bars coloured by class, the outdoor forecast on a
second axis, a numbered period lane, a "now" marker and a touch/hover tooltip
(time, price, class, window median, what the controller does in that class,
interpolated outdoor temperature). Each period gets a sentence built from its own
numbers, e.g. *"Cheap period 15/11, 23:30 - 01:00: average price 7.12 c/kWh
(8 % below the comparison median). Cheap: heating is allowed above the target, up
to +1.0 C."* In Finnish and English.

## What it deliberately does NOT show

* **No predicted indoor temperature and no cost estimate.** Both need a
  calibrated building model, which does not exist; the MPC that could produce
  them runs only on a synthetic model and is shadow-only. The card says why.
* **No claim that the weather forecast shapes the decision.** The live controller
  does not use outdoor temperature; the forecast is drawn as context and the card
  states it.
* Future periods are *intent*, not a schedule: the controller decides every 15
  minutes from the measured room temperature.
* Gaps, NaN or out-of-range prices truncate the series at the gap (never bridged);
  with no fresh prices the card shows an explanation instead of an empty chart.

## Evidence

| Check | Result |
|---|---|
| `plan_timeline_tests`: for every slot of 3 zones x 3 profiles x several 48 h views (~thousands of slots) the timeline class equals the controller's reaction (cheap -> `CheapWindow`, expensive -> `ExpensiveWindow`); all three classes occur; gaps/alignment/NaN/capacity/JSON validity/no forbidden fields | pass |
| `tests/test_plan_timeline.js`: validation, layout (negative and flat prices), hit testing, outdoor interpolation without bridging, texts, tick rule, every literal has English | pass |
| Host C++ Debug/Release, Node tests, Python failing set (= `main`'s 5) | pass / pass / identical |
| Firmware `esp32s3_cottage_shadow` | SUCCESS (PSRAM buffers, chunked JSON, no cJSON tree) |
| Browser on simulator | chart, tooltip on touch, profile switch, FI/EN, empty and gap cases |

`price_bootstrap.hpp` was refactored to share the classification; the existing
`cottage_price_bootstrap_tests` and the profile tests still pass unchanged.

## Open

* No device run; JSON size (~25 KB for 192 slots) is streamed in 768-byte chunks but
  has not been measured on hardware.
* Indoor-temperature and cost projections remain open until the building model is
  calibrated (`OPTIMIZATION_AUDIT_20261118.md`, L22).
* Past prices and measured indoor history are not drawn on this chart.

## Ohjausrajoitusten ja tilan oikea tulkinta

Aikajanan nykyinen toiminto on **ohjausehdotus** (`Ehdotus: lämmitys`,
`Ehdotus: lämmitys pois` tai `Ehdotus: nykyinen tila pidetään`).
Se ei kerro pumpun todellista toimintatilaa eikä väitä, että komento olisi
lähetetty. Fyysinen ohjaus edellyttää aina sen omia käyttöönotto- ja
turvaedellytyksiä.

Kuivanapidon laskettu lämpötilatavoite säilytetään historiassa ja
käyttöliittymässä myös silloin, kun kosteusmittauksen ehdot **eivät
salli lämmitystä**. `heating_permitted:false` siirtyy
`/api/timeline`-vastaukseen suoraan saman
`drying_blocks_heat(plan)`-ehdon perusteella kuin ohjauksessa;
aikajana ei silloin ehdota hintaikkunan mukaista lämmitystä.
Kylmillään-tilan puuttuvaa lämpötilatavoitetta käsitellään edelleen
omana tapauksena. Nämä ovat lähdekoodi- ja host-testattuja ehtoja,
eivät fyysisen pumpun todennuksia.
