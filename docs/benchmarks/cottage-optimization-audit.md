| Testitapaus | Jaksot | Perusohjaus € | Hintaraja € | Enevina MPC € | MILP € | Säästö vs perus % | Ero MILP € % | Ero MILP tavoite % | MPC käynn. | MPC ms (x86) | Rajoitukset / huom. |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| base | 24 | 0.604 | 0.605 | 0.572 | 0.572 | 5.2 | 0.00 | 0.00 | 1 | 0.5 | OK |
| base | 48 | 1.388 | 1.299 | 1.195 | 1.195 | 13.9 | 0.00 | 0.00 | 2 | 1.3 | OK |
| base | 96 | 2.761 | 2.648 | 2.296 | 2.274 | 16.8 | 0.96 | 0.90 | 3 | 5.2 | OK |
| base | 192 | 5.447 | 5.259 | 4.559 | 4.555 | 16.3 | 0.10 | 0.09 | 5 | 9.6 | OK |
| flat_price | 24 | 0.818 | 0.818 | 0.771 | 0.771 | 5.7 | 0.00 | 0.00 | 1 | 0.4 | OK |
| flat_price | 48 | 1.602 | 1.541 | 1.484 | 1.484 | 7.4 | 0.00 | 0.00 | 2 | 1.4 | OK |
| flat_price | 96 | 2.989 | 2.925 | 2.817 | 2.815 | 5.8 | 0.04 | 0.04 | 3 | 12.2 | MILP: aikaraja, ei todistettu optimi |
| flat_price | 192 | 5.910 | 5.850 | 5.527 | 5.659 | 6.5 | -2.33 | -3.14 | 7 | 8.9 | MILP: aikaraja, ei todistettu optimi |
| price_spikes | 24 | 0.627 | 0.627 | 0.591 | 0.591 | 5.7 | 0.00 | 0.00 | 1 | 0.5 | OK |
| price_spikes | 48 | 3.487 | 2.527 | 1.154 | 1.154 | 66.9 | 0.00 | 0.00 | 2 | 1.7 | OK |
| price_spikes | 96 | 7.227 | 5.385 | 2.163 | 2.205 | 70.1 | -1.94 | 0.73 | 4 | 4.2 | price: loppuehto 12 jakson lämmitysikkunalla |
| price_spikes | 192 | 14.362 | 11.674 | 4.411 | 4.489 | 69.3 | -1.72 | -0.36 | 6 | 9.2 | price: loppuehto 12 jakson lämmitysikkunalla; MILP: aikaraja, ei todistettu optimi |
| negative_prices | 24 | 0.690 | 0.690 | 0.651 | 0.651 | 5.7 | 0.00 | 0.00 | 1 | 0.3 | OK |
| negative_prices | 48 | 1.097 | 1.043 | 0.905 | 0.905 | 17.5 | 0.00 | 0.00 | 2 | 1.4 | OK |
| negative_prices | 96 | 2.022 | 1.915 | 1.514 | 1.514 | 25.1 | 0.00 | 0.00 | 3 | 3.9 | OK |
| negative_prices | 192 | 4.072 | 3.885 | 3.035 | 3.029 | 25.5 | 0.17 | 0.16 | 6 | 10.7 | MILP: aikaraja, ei todistettu optimi |
| expensive_night_cheap_day | 24 | 2.091 | 2.091 | 1.972 | 1.972 | 5.7 | 0.00 | 0.00 | 1 | 0.4 | OK |
| expensive_night_cheap_day | 48 | 2.688 | 2.514 | 2.145 | 2.145 | 20.2 | 0.00 | 0.00 | 3 | 1.4 | OK |
| expensive_night_cheap_day | 96 | 4.045 | 3.839 | 3.098 | 3.098 | 23.4 | 0.00 | 0.00 | 3 | 4.3 | OK |
| expensive_night_cheap_day | 192 | 8.322 | 7.767 | 5.817 | 5.820 | 30.1 | -0.06 | -1.01 | 6 | 10.2 | MILP: aikaraja, ei todistettu optimi |
| cheap_night_expensive_day | 24 | 0.372 | 0.372 | 0.351 | 0.351 | 5.7 | 0.00 | 0.00 | 1 | 0.4 | OK |
| cheap_night_expensive_day | 48 | 2.066 | 1.751 | 1.208 | 1.208 | 41.5 | 0.00 | 0.00 | 2 | 1.5 | OK |
| cheap_night_expensive_day | 96 | 4.778 | 4.368 | 3.401 | 3.399 | 28.8 | 0.05 | 0.05 | 4 | 4.5 | OK |
| cheap_night_expensive_day | 192 | 9.139 | 8.736 | 6.679 | 6.682 | 26.9 | -0.04 | -0.04 | 7 | 9.3 | MILP: aikaraja, ei todistettu optimi |
| hard_frost | 24 | 1.195 | 1.199 | 1.203 | 1.203 | -0.6 | 0.00 | 0.00 | 1 | 0.2 | thermostat: loppuehto 12 jakson lämmitysikkunalla |
| hard_frost | 48 | 2.933 | 2.834 | 2.672 | 2.652 | 8.9 | 0.78 | 0.75 | 2 | 0.8 | thermostat: loppuehto 12 jakson lämmitysikkunalla; price: loppuehto 12 jakson lämmitysikkunalla |
| hard_frost | 96 | 5.785 | 5.710 | 5.296 | 5.296 | 8.5 | 0.00 | 0.00 | 3 | 3.4 | thermostat: loppuehto 12 jakson lämmitysikkunalla; price: loppuehto 16 jakson lämmitysikkunalla |
| hard_frost | 192 | 11.542 | 11.290 | 10.538 | 10.538 | 8.7 | 0.00 | 0.00 | 5 | 8.8 | price: loppuehto 12 jakson lämmitysikkunalla |
| mild | 24 | 0.267 | 0.267 | 0.176 | 0.176 | 34.1 | 0.00 | 0.00 | 1 | 0.7 | OK |
| mild | 48 | 0.462 | 0.462 | 0.292 | 0.288 | 36.7 | 1.38 | 1.05 | 2 | 1.7 | OK |
| mild | 96 | 0.671 | 0.813 | 0.383 | 0.379 | 42.9 | 1.13 | 0.92 | 2 | 11.3 | OK |
| mild | 192 | 1.316 | 1.485 | 0.794 | 0.785 | 39.7 | 1.18 | 0.99 | 3 | 9.9 | MILP: aikaraja, ei todistettu optimi |
| fast_outdoor_drop | 24 | 0.269 | 0.303 | 0.245 | 0.245 | 8.8 | 0.00 | 0.00 | 1 | 0.5 | OK |
| fast_outdoor_drop | 48 | 0.674 | 0.719 | 0.510 | 0.507 | 24.4 | 0.44 | 0.37 | 2 | 2.7 | OK |
| fast_outdoor_drop | 96 | 3.561 | 3.530 | 3.244 | 3.219 | NOT_COMPARABLE | 0.78 | 0.76 | 2 | 15.1 | thermostat: 2 rajarikettä; thermostat: loppuehto 64 jakson lämmitysikkunalla; price: 1 rajarikettä; price: loppuehto 64 jakson lämmitysikkunalla |
| fast_outdoor_drop | 192 | 7.902 | 7.652 | — | — | NOT_COMPARABLE | — | — | — | 4.8 | thermostat: 97 rajarikettä; thermostat: loppuehto ei täyty (4.93 °C), NOT_COMPARABLE; thermostat: loppuehto 192 jakson lämmitysikkunalla; price: 111 rajarikettä; price: loppuehto ei täyty (5.10 °C), NOT_COMPARABLE; price: loppuehto 192 jakson lämmitysikkunalla; mpc: INFEASIBLE; MILP: ei ratkaisua (status 2) |
| slow_building | 24 | 0.570 | 0.572 | 0.502 | 0.502 | 11.9 | 0.00 | 0.00 | 1 | 0.3 | OK |
| slow_building | 48 | 1.203 | 1.104 | 0.984 | 0.984 | 18.2 | 0.00 | 0.00 | 1 | 1.9 | OK |
| slow_building | 96 | 2.296 | 2.079 | 1.894 | 1.858 | 17.5 | 1.96 | 1.81 | 3 | 3.5 | price: loppuehto 12 jakson lämmitysikkunalla |
| slow_building | 192 | 4.490 | 4.071 | 3.633 | 3.533 | 19.1 | 2.84 | 1.06 | 4 | 13.8 | OK |
| fast_cooling_building | 24 | 0.792 | 0.839 | 0.838 | 0.838 | NOT_COMPARABLE | 0.00 | 0.00 | 2 | 0.1 | thermostat: 4 rajarikettä; price: 4 rajarikettä |
| fast_cooling_building | 48 | 1.828 | 1.827 | 1.891 | 1.891 | NOT_COMPARABLE | 0.00 | 0.00 | 3 | 0.9 | thermostat: 10 rajarikettä; price: 7 rajarikettä |
| fast_cooling_building | 96 | 3.549 | 3.532 | 3.697 | 3.736 | NOT_COMPARABLE | -1.02 | -0.93 | 6 | 3.4 | thermostat: 17 rajarikettä; price: 12 rajarikettä; MILP: aikaraja, ei todistettu optimi |
| fast_cooling_building | 192 | 7.004 | 6.995 | 7.454 | 7.833 | NOT_COMPARABLE | -4.84 | -4.12 | 11 | 7.4 | thermostat: 37 rajarikettä; price: 27 rajarikettä; MILP: aikaraja, ei todistettu optimi |
| oversized_pump | 24 | 0.625 | 0.626 | 0.571 | 0.571 | 8.7 | 0.01 | 0.01 | 2 | 0.5 | OK |
| oversized_pump | 48 | 1.440 | 1.400 | 1.179 | 1.179 | 18.1 | 0.00 | 0.00 | 2 | 1.9 | OK |
| oversized_pump | 96 | 2.896 | 2.795 | 2.326 | 2.326 | 19.7 | 0.00 | 0.00 | 3 | 3.9 | OK |
| oversized_pump | 192 | 5.830 | 5.581 | 4.646 | 4.692 | 20.3 | -0.98 | -0.91 | 6 | 9.4 | MILP: aikaraja, ei todistettu optimi |
| undersized_pump | 24 | 0.524 | 0.526 | 0.501 | 0.501 | 4.4 | 0.00 | 0.00 | 1 | 0.3 | OK |
| undersized_pump | 48 | 1.235 | 1.194 | 1.139 | 1.125 | 7.7 | 1.23 | 1.14 | 2 | 1.0 | thermostat: loppuehto 12 jakson lämmitysikkunalla; price: loppuehto 12 jakson lämmitysikkunalla |
| undersized_pump | 96 | 2.536 | 2.410 | 2.237 | 2.207 | 11.8 | 1.39 | 1.30 | 3 | 4.4 | thermostat: loppuehto 16 jakson lämmitysikkunalla; price: loppuehto 12 jakson lämmitysikkunalla |
| undersized_pump | 192 | 4.950 | 4.777 | 4.309 | 4.293 | 12.9 | 0.37 | 0.35 | 5 | 10.2 | price: loppuehto 12 jakson lämmitysikkunalla |
| poor_cold_cop | 24 | 2.040 | 2.145 | 2.053 | 2.053 | -0.6 | 0.00 | 0.00 | 1 | 0.2 | OK |
| poor_cold_cop | 48 | 4.514 | 4.477 | 4.017 | 4.017 | 11.0 | 0.00 | 0.00 | 2 | 1.0 | OK |
| poor_cold_cop | 96 | 8.235 | 8.145 | 7.406 | 7.318 | 10.1 | 1.21 | 1.18 | 3 | 4.0 | OK |
| poor_cold_cop | 192 | 16.345 | 16.305 | 14.641 | 14.591 | 10.4 | 0.34 | 0.33 | 5 | 9.0 | OK |
| defrost_derating | 24 | 0.595 | 0.612 | 0.581 | 0.581 | 2.2 | 0.00 | 0.00 | 1 | 0.3 | OK |
| defrost_derating | 48 | 1.357 | 1.354 | 1.225 | 1.225 | 9.7 | 0.00 | 0.00 | 2 | 1.2 | OK |
| defrost_derating | 96 | 2.703 | 2.696 | 2.302 | 2.280 | 14.8 | 0.97 | 0.91 | 3 | 4.2 | price: loppuehto 12 jakson lämmitysikkunalla |
| defrost_derating | 192 | 5.412 | 5.260 | 4.596 | 4.559 | 15.1 | 0.81 | 0.76 | 5 | 10.7 | OK |
| dst_autumn | 24 | 0.604 | 0.605 | 0.572 | 0.572 | 5.2 | 0.00 | 0.00 | 1 | 0.4 | OK |
| dst_autumn | 48 | 1.368 | 1.294 | 1.195 | 1.191 | 12.6 | 0.36 | 0.34 | 2 | 1.9 | OK |
| dst_autumn | 96 | 2.761 | 2.663 | 2.315 | 2.294 | 16.1 | 0.94 | 0.88 | 3 | 5.4 | OK |
| dst_autumn | 192 | 5.447 | 5.279 | 4.600 | 4.595 | 15.6 | 0.10 | 0.09 | 5 | 9.8 | OK |
| dst_spring | 24 | 0.604 | 0.605 | 0.572 | 0.572 | 5.2 | 0.00 | 0.00 | 1 | 0.4 | OK |
| dst_spring | 48 | 1.398 | 1.320 | 1.216 | 1.207 | 13.1 | 0.74 | 0.69 | 2 | 1.9 | OK |
| dst_spring | 96 | 2.750 | 2.658 | 2.270 | 2.269 | 17.5 | 0.02 | 0.02 | 3 | 3.9 | OK |
| dst_spring | 192 | 5.452 | 5.280 | 4.550 | 4.536 | 16.6 | 0.31 | 0.29 | 5 | 9.4 | OK |
| stale_weather_forecast | 24 | 0.604 | 0.605 | 0.642 | 0.572 | -6.4 | — | — | 1 | 0.4 | OK |
| stale_weather_forecast | 48 | 1.388 | 1.299 | 1.166 | 1.195 | — | — | — | 2 | 2.0 | mpc: loppuehto ei täyty (0.16 °C), NOT_COMPARABLE |
| stale_weather_forecast | 96 | 2.761 | 2.648 | 2.226 | 2.274 | — | — | — | 3 | 4.8 | mpc: loppuehto ei täyty (1.06 °C), NOT_COMPARABLE |
| stale_weather_forecast | 192 | 5.447 | 5.259 | 4.485 | 4.555 | — | — | — | 6 | 13.0 | mpc: 2 rajarikettä; mpc: loppuehto ei täyty (0.45 °C), NOT_COMPARABLE; MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 24 | 0.601 | 0.601 | 0.783 | 0.572 | -30.1 | — | — | 1 | 0.3 | OK |
| stale_weather_forecast_margin1 | 48 | 1.395 | 1.388 | 1.218 | 1.195 | 12.7 | — | — | 2 | 2.1 | MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 96 | 2.723 | 2.694 | 2.573 | 2.274 | 5.5 | — | — | 3 | 5.5 | MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 192 | 5.429 | 5.405 | 5.139 | 4.555 | 5.3 | — | — | 8 | 12.8 | price: loppuehto 12 jakson lämmitysikkunalla; MPC: FALLBACK_REPLAY_FAILED |

Ei taulukossa (katso C++-vikatestit):
- `supply_water_cop_penalty`: NOT_APPLICABLE: air-to-air pump has no supply water; covered by hydronic G0 (tools/reference_benchmark.py)
- `missing_price_slot`: fault path: BridgeStatus::Gap -> no MPC plan (C++ test)
- `sensor_outage`: fault path: bootstrap UNAVAILABLE, MPC not run (C++ test)
- `pump_link_outage`: fault path: control gate INPUTS_UNVERIFIED (C++ test)
- `optimizer_timeout`: fault path: MpcStatus::Timeout, count=0, no partial plan (C++ test)
- `no_feasible_plan`: fault path: UNSAFE_START / INFEASIBLE, no plan (C++ test)
