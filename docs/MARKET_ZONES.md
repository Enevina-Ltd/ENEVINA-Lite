# Countries, bidding zones, currencies and civil time (19.11.2026)

**Status: host-tested against real recorded responses; firmware builds
(`esp32s3_cottage_shadow`). NOT yet verified on a physical ESP32-S3 board.**

## What "supported" means

A bidding zone is listed in `forecast/market_zones.hpp` only if the Nord Pool
Data Portal day-ahead endpoint returned a **complete 15-minute market day for
that zone in a real request** (fetched 2026-11-19, EUR series, delivery date
2026-11-18; DST day 2026-10-25 for FI, SE3 and NL). The recorded responses are
in `tests/data/nordpool/` and are parsed by `tests/market_zones_tests.cpp`.
Weather coverage is global and is **never** used to infer price support.

| Country | Zones (UI value → Nord Pool `deliveryArea`) | Currency | Std. UTC offset |
|---|---|---|---|
| Finland | FI | EUR | +2 |
| Sweden | SE1 Luleå, SE2 Sundsvall, SE3 Stockholm, SE4 Malmö | SEK | +1 |
| Norway | NO1 Oslo, NO2 Kristiansand, NO3 Trondheim, NO4 Tromsø, NO5 Bergen | NOK | +1 |
| Denmark | DK1 West, DK2 East (Copenhagen) | DKK | +1 |
| Estonia / Latvia / Lithuania | EE / LV / LT | EUR | +2 |
| Germany (with Luxembourg) | DE-LU → `GER` | EUR | +1 |
| Netherlands, Belgium, France, Austria | NL, BE, FR, AT | EUR | +1 |
| Poland | PL | PLN | +1 |
| Bulgaria | BG | EUR | +2 |

Total: 14 countries, 22 bidding zones.

### Not supported (checked 2026-11-19, no data returned)

Spain, Portugal, Italy, Great Britain, Ireland, Switzerland, Czechia, Hungary,
Slovakia, Slovenia, Romania, Croatia, Greece and others returned HTTP 204 (no
data) from the Nord Pool Data Portal. Supporting them needs another source
(for example the ENTSO-E Transparency Platform, which requires a security token
and a legal/terms review) — **a fallback source is not implemented**. The
dashboard must say so instead of showing a guessed price.

## Rules the code enforces

* **Zone is never guessed.** A multi-zone country (SE, NO, DK) has no zone until
  the user picks it; with no zone, no prices are fetched (`market_set_region(nullptr,nullptr)`).
  A city name does not choose the zone.
* **Currency.** Spot prices are requested directly in the zone's currency
  (`currency=SEK` etc.; Nord Pool applies the day's exchange rate). The
  tariff the user types is in the same currency, so no exchange rate is stored
  on the device and spot and tariff are never mixed across currencies.
  Identifiers such as `spot_eur_per_kwh` predate this; `MarketStatus::currency`
  and `/api/status.price_currency` are authoritative.
* **Market day.** Nord Pool delivery dates are CET/CEST calendar dates for every
  zone (a Finnish zone's day 2026-11-18 starts 2026-11-17T23:00Z).
  `nordpool_market_date()` computes them without any host time-zone database.
  This also fixes an old Finland-only defect: between 00:00 and 01:00 Finnish
  time the Finnish *local* date was one day ahead of the market date, so the
  current slot's price was never fetched in that hour.
* **DST.** A market day has 92, 96 or 100 slots; any other length is rejected.
  Local time for the transfer schedule uses the EU rule with the country's
  standard offset (`eu_local_time`).
* **Plausibility limits scale with currency** (`currency_limit_scale`): a
  transfer price of 0.60 SEK/kWh is normal, 0.60 EUR/kWh is rejected.
* **Changing country** clears the previous contract amounts and VAT and marks the
  contract unconfirmed (they would otherwise be silently wrong); pump, sensors and
  comfort settings are kept. Changing zone or country discards the price cache
  (RAM and NVS) so another zone's prices are never reused.
* **No invented defaults.** VAT, tax and transfer prices outside Finland are asked
  from the user; `tariff_confirmed` stays `false` until they are entered.
* Upgrades: a stored v1 settings blob (Finland only) is migrated to v2 keeping
  every value, the wizard does not reappear (`site_settings_from_v1`).

## Open items before commercial use (L19)

* Nord Pool Data Portal usage terms / licence for a commercial product are not
  yet reviewed; the endpoint is the same one the existing Finland reader used.
* A second price source for resilience (ENTSO-E) is not implemented.
