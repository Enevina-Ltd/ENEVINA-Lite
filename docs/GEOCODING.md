# City search (geocoding) for setup and weather (19.11.2026)

**Status: parser and filters host-tested against real recorded responses;
firmware route builds. The HTTPS request has NOT been run on a physical device.**

## Flow

1. The browser calls `GET /api/geocode?q=Kang&country=FI&lang=fi` on the device
   (home LAN only, like every other `/api/*` route). The phone never talks to the
   geocoder, so it also works when the phone has no internet route of its own.
2. The device starts **one** background worker (`cottage_geocode.cpp`), returns
   `202 {"state":"pending"}` immediately and the page polls the same URL. The web
   server is never blocked by the network. A different search while one runs is
   refused with `error:"BUSY"`; the same query is answered from the last result for
   120 s; at most one new request per 2 s.
3. The result list (`name`, `region`, `country`, `lat`, `lon`, `latlon`) is
   validated by `forecast/geocode.hpp`. The chosen entry's `latlon` is saved with
   the existing `fmi_latlon` setting, which already drives the weather reader and
   the cache invalidation. Manual `lat,lon` entry remains available.

## Why Photon

The spec example ("Kang" → Kangasala) is a *prefix* search. Open-Meteo's geocoder
(tested 2026-11-19) matches whole names: `Kang` returns eight hamlets called
"Kangas" and Kangasala only appears from `Kangasa`. Photon (OpenStreetMap data)
returns Kangasala, FI for `Kang` (`tests/data/geocode/photon_kang.json`). Results
are filtered to populated places (`place=city|town|village|hamlet|municipality|
suburb|…` and administrative boundaries); farmyards, single dwellings and land-use
polygons are dropped. Query text is percent-encoded and length-limited; names are
accepted only if they are valid UTF-8 without markup characters.

TLS: root certificate ISRG Root X1 (`photon_ca_pem`, SHA-256 fingerprint checked
against the published value; valid until 2035-06-04). No insecure fallback.

## Open items before commercial use (L19)

* The public Photon instance is a fair-use service; a commercial product needs
  the operator's permission, a paid/own instance, or another provider. The code
  has a single URL builder (`build_geocode_url`) so the provider can be swapped.
* Data © OpenStreetMap contributors (ODbL): attribution must be shown in the UI
  before release (not yet added).
* The geocoder returns no time zone; the civil time zone is derived from the
  selected supported country (`MarketCountry::iana`).
