# MET Norway weather in Enevina Lite

Enevina Lite uses [MET Norway Locationforecast 2.0](https://api.met.no/weatherapi/locationforecast/2.0/documentation) for outdoor temperature forecasts. The service provides **worldwide point coverage**, so Finland is not a special case.

## Attribution and legal requirements

**Weather data © MET Norway, CC BY 4.0.** Original data: https://api.met.no/. Enevina Lite selects, validates and displays a bounded hourly temperature series; these steps process the original data. MET Norway and Yr do not sponsor or endorse Enevina Lite.

**Before enabling this provider in distributed devices, the responsible human at Enevina Ltd should review and accept the [MET Weather API Terms](https://api.met.no/doc/TermsOfService).** These terms require identification, appropriate caching, attribution, and considerate request rates. The device sends its public IP and selected geographic coordinates to MET Norway; these may appear in MET Norway access logs.

## Technical design

1. Device requests the compact forecast over HTTPS at up to four-decimal coordinate precision; the browser never calls MET Norway directly.
2. Device identifies itself with an application User-Agent and accepts gzip/deflate. The ESP32-S3 ROM handles bounded decompression, including gzip CRC verification. Forecast buffers use PSRAM.
3. A no-allocation JSON parser validates the document and extracts strictly consecutive hourly UTC temperature points. Six-hour medium-range values are not falsely interpolated as hourly measurements.
4. A versioned, checksummed weather cache tracks Expires and Last-Modified. The client waits for the expiry, uses If-Modified-Since, accepts 304 only if a valid matching-location cache exists, and backs off on HTTP 429.
5. Changing a persisted weather location discards the old-location cache and wakes the reader without a restart or changing CN105 authorization.
6. The dashboard displays up to 48 forecast hours and credits MET Norway with a CC BY 4.0 link.

## Country coverage and limits

Locationforecast 2.0 supports point locations globally: Finland, Norway, Sweden, Denmark, the Baltic region, Germany, the UK and countries outside Europe. Supported electricity price bidding zones are a **separate** product feature and must not be inferred from weather coverage.

Although the upstream service offers forecasts for up to nine days, Enevina Lite currently uses only the consecutive hourly segment and shows the first 48 hours. Precipitation, wind and humidity fields are available in the upstream API but are not currently integrated into the Lite optimizer.

MET Norway is a best-effort external weather forecast, not a frost-protection or heating-safety guarantee.
