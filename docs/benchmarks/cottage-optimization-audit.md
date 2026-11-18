| Testitapaus | Jaksot | Perusohjaus € | Hintaraja € | Enevina MPC € | MILP € | Säästö vs perus % | Ero MILP € % | Ero MILP tavoite % | MPC käynn. | MPC ms (x86) | Rajoitukset / huom. |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| base | 24 | 0.604 | 0.605 | 0.572 | 0.572 | 5.2 | 0.00 | 0.00 | 1 | 0.5 | OK |
| base | 48 | 1.388 | 1.309 | 1.195 | 1.195 | 13.9 | 0.00 | 0.00 | 2 | 1.6 | OK |
| base | 96 | 2.761 | 2.667 | 2.296 | 2.274 | 16.8 | 0.96 | 0.90 | 3 | 8.6 | OK |
| base | 192 | 5.447 | 5.297 | 4.559 | 4.555 | 16.3 | 0.10 | 0.09 | 5 | 14.7 | OK |
| flat_price | 24 | 0.818 | 0.818 | 0.771 | 0.771 | 5.7 | 0.00 | 0.00 | 1 | 0.3 | OK |
| flat_price | 48 | 1.602 | 1.541 | 1.484 | 1.484 | 7.4 | 0.00 | 0.00 | 2 | 1.4 | OK |
| flat_price | 96 | 2.989 | 2.925 | 2.817 | 2.816 | 5.8 | 0.02 | 0.02 | 3 | 8.4 | MILP: aikaraja, ei todistettu optimi |
| flat_price | 192 | 5.910 | 5.850 | 5.527 | 5.659 | 6.5 | -2.33 | -3.14 | 7 | 13.0 | MILP: aikaraja, ei todistettu optimi |
| price_spikes | 24 | 0.627 | 0.627 | 0.591 | 0.591 | 5.7 | 0.00 | 0.00 | 1 | 0.5 | OK |
| price_spikes | 48 | 3.487 | 2.527 | 1.154 | 1.154 | 66.9 | 0.00 | 0.00 | 2 | 1.9 | OK |
| price_spikes | 96 | 7.227 | 5.385 | 2.163 | 2.205 | 70.1 | -1.94 | 0.73 | 4 | 5.4 | OK |
| price_spikes | 192 | 14.362 | 11.242 | 4.411 | 4.489 | 69.3 | -1.72 | -0.36 | 6 | 13.8 | MILP: aikaraja, ei todistettu optimi |
| negative_prices | 24 | 0.690 | 0.690 | 0.651 | 0.651 | 5.7 | 0.00 | 0.00 | 1 | 0.4 | OK |
| negative_prices | 48 | 1.097 | 1.087 | 0.905 | 0.905 | 17.5 | 0.00 | 0.00 | 2 | 2.0 | OK |
| negative_prices | 96 | 2.022 | 2.004 | 1.514 | 1.514 | 25.1 | 0.00 | 0.00 | 3 | 5.8 | OK |
| negative_prices | 192 | 4.072 | 3.973 | 3.035 | 3.029 | 25.5 | 0.17 | 0.16 | 6 | 13.4 | MILP: aikaraja, ei todistettu optimi |
| expensive_night_cheap_day | 24 | 2.091 | 2.091 | 1.972 | 1.972 | 5.7 | 0.00 | 0.00 | 1 | 0.4 | OK |
| expensive_night_cheap_day | 48 | 2.688 | 2.658 | 2.145 | 2.145 | 20.2 | 0.00 | 0.00 | 3 | 1.7 | OK |
| expensive_night_cheap_day | 96 | 4.045 | 4.016 | 3.098 | 3.098 | 23.4 | 0.00 | 0.00 | 3 | 5.8 | OK |
| expensive_night_cheap_day | 192 | 8.322 | 7.944 | 5.817 | 5.820 | 30.1 | -0.06 | -1.01 | 6 | 12.4 | MILP: aikaraja, ei todistettu optimi |
| cheap_night_expensive_day | 24 | 0.372 | 0.372 | 0.351 | 0.351 | 5.7 | 0.00 | 0.00 | 1 | 0.5 | OK |
| cheap_night_expensive_day | 48 | 2.066 | 1.751 | 1.208 | 1.208 | 41.5 | 0.00 | 0.00 | 2 | 1.6 | OK |
| cheap_night_expensive_day | 96 | 4.778 | 4.368 | 3.401 | 3.399 | 28.8 | 0.05 | 0.05 | 4 | 4.3 | OK |
| cheap_night_expensive_day | 192 | 9.139 | 8.736 | 6.679 | 6.682 | 26.9 | -0.04 | -0.04 | 7 | 9.7 | MILP: aikaraja, ei todistettu optimi |
| hard_frost | 24 | 1.195 | 1.199 | 1.203 | 1.203 | -0.6 | 0.00 | 0.00 | 1 | 0.2 | thermostat: loppuehto 12 jakson lämmitysikkunalla |
| hard_frost | 48 | 2.933 | 2.834 | 2.672 | 2.652 | 8.9 | 0.78 | 0.75 | 2 | 1.1 | thermostat: loppuehto 12 jakson lämmitysikkunalla; price: loppuehto 12 jakson lämmitysikkunalla |
| hard_frost | 96 | 5.785 | 5.797 | 5.296 | 5.296 | 8.5 | 0.00 | 0.00 | 3 | 3.6 | thermostat: loppuehto 12 jakson lämmitysikkunalla |
| hard_frost | 192 | 11.542 | 11.409 | 10.538 | 10.538 | 8.7 | 0.00 | 0.00 | 5 | 11.4 | OK |
| mild | 24 | 0.267 | 0.267 | 0.176 | 0.176 | 34.1 | 0.00 | 0.00 | 1 | 0.6 | OK |
| mild | 48 | 0.462 | 0.462 | 0.292 | 0.288 | 36.7 | 1.38 | 1.05 | 2 | 2.1 | OK |
| mild | 96 | 0.671 | 0.587 | 0.383 | 0.379 | 42.9 | 1.13 | 0.92 | 2 | 5.1 | OK |
| mild | 192 | 1.316 | 1.189 | 0.794 | 0.785 | 39.7 | 1.18 | 0.99 | 3 | 11.0 | MILP: aikaraja, ei todistettu optimi |
| fast_outdoor_drop | 24 | 0.269 | 0.303 | 0.245 | 0.245 | 8.8 | 0.00 | 0.00 | 1 | 0.7 | OK |
| fast_outdoor_drop | 48 | 0.674 | 0.719 | 0.510 | 0.507 | 24.4 | 0.44 | 0.37 | 2 | 2.6 | OK |
| fast_outdoor_drop | 96 | 3.561 | 3.530 | 3.244 | 3.219 | NOT_COMPARABLE | 0.78 | 0.76 | 2 | 4.3 | thermostat: 2 rajarikettä; thermostat: loppuehto 64 jakson lämmitysikkunalla; price: 1 rajarikettä; price: loppuehto 64 jakson lämmitysikkunalla |
| fast_outdoor_drop | 192 | 7.902 | 7.940 | — | — | NOT_COMPARABLE | — | — | — | 7.0 | thermostat: 97 rajarikettä; thermostat: loppuehto ei täyty (4.93 °C), NOT_COMPARABLE; thermostat: loppuehto 192 jakson lämmitysikkunalla; price: 91 rajarikettä; price: loppuehto ei täyty (4.85 °C), NOT_COMPARABLE; price: loppuehto 192 jakson lämmitysikkunalla; mpc: INFEASIBLE; MILP: ei ratkaisua (status 2) |
| slow_building | 24 | 0.570 | 0.572 | 0.502 | 0.502 | 11.9 | 0.00 | 0.00 | 1 | 0.4 | OK |
| slow_building | 48 | 1.203 | 1.104 | 0.984 | 0.984 | 18.2 | 0.00 | 0.00 | 1 | 1.6 | OK |
| slow_building | 96 | 2.296 | 2.139 | 1.894 | 1.858 | 17.5 | 1.96 | 1.81 | 3 | 5.7 | OK |
| slow_building | 192 | 4.490 | 4.069 | 3.633 | 3.533 | 19.1 | 2.84 | 1.06 | 4 | 14.8 | OK |
| fast_cooling_building | 24 | 0.792 | 0.839 | 0.838 | 0.838 | NOT_COMPARABLE | 0.00 | 0.00 | 2 | 0.2 | thermostat: 4 rajarikettä; price: 4 rajarikettä |
| fast_cooling_building | 48 | 1.828 | 1.827 | 1.891 | 1.891 | NOT_COMPARABLE | 0.00 | 0.00 | 3 | 0.7 | thermostat: 10 rajarikettä; price: 7 rajarikettä |
| fast_cooling_building | 96 | 3.549 | 3.542 | 3.697 | 3.736 | NOT_COMPARABLE | -1.02 | -0.93 | 6 | 2.8 | thermostat: 17 rajarikettä; price: 12 rajarikettä; MILP: aikaraja, ei todistettu optimi |
| fast_cooling_building | 192 | 7.004 | 7.017 | 7.454 | 7.833 | NOT_COMPARABLE | -4.84 | -4.12 | 11 | 9.0 | thermostat: 37 rajarikettä; price: 27 rajarikettä; MILP: aikaraja, ei todistettu optimi |
| oversized_pump | 24 | 0.625 | 0.626 | 0.571 | 0.571 | 8.7 | 0.01 | 0.01 | 2 | 0.7 | OK |
| oversized_pump | 48 | 1.440 | 1.411 | 1.179 | 1.179 | 18.1 | 0.00 | 0.00 | 2 | 2.0 | OK |
| oversized_pump | 96 | 2.896 | 2.805 | 2.326 | 2.326 | 19.7 | 0.00 | 0.00 | 3 | 6.0 | OK |
| oversized_pump | 192 | 5.830 | 5.509 | 4.646 | 4.692 | 20.3 | -0.98 | -0.91 | 6 | 9.6 | MILP: aikaraja, ei todistettu optimi |
| undersized_pump | 24 | 0.524 | 0.526 | 0.501 | 0.501 | 4.4 | 0.00 | 0.00 | 1 | 0.3 | OK |
| undersized_pump | 48 | 1.235 | 1.194 | 1.139 | 1.125 | 7.7 | 1.23 | 1.14 | 2 | 1.1 | thermostat: loppuehto 12 jakson lämmitysikkunalla |
| undersized_pump | 96 | 2.536 | 2.481 | 2.237 | 2.207 | 11.8 | 1.39 | 1.30 | 3 | 3.1 | thermostat: loppuehto 16 jakson lämmitysikkunalla; price: loppuehto 24 jakson lämmitysikkunalla |
| undersized_pump | 192 | 4.950 | 4.869 | 4.309 | 4.293 | 12.9 | 0.37 | 0.35 | 5 | 7.8 | OK |
| poor_cold_cop | 24 | 2.040 | 2.145 | 2.053 | 2.053 | -0.6 | 0.00 | 0.00 | 1 | 0.2 | OK |
| poor_cold_cop | 48 | 4.514 | 4.477 | 4.017 | 4.017 | 11.0 | 0.00 | 0.00 | 2 | 1.1 | OK |
| poor_cold_cop | 96 | 8.235 | 8.165 | 7.406 | 7.318 | 10.1 | 1.21 | 1.18 | 3 | 4.3 | OK |
| poor_cold_cop | 192 | 16.345 | 16.325 | 14.641 | 14.591 | 10.4 | 0.34 | 0.33 | 5 | 8.6 | OK |
| defrost_derating | 24 | 0.595 | 0.612 | 0.581 | 0.581 | 2.2 | 0.00 | 0.00 | 1 | 0.4 | OK |
| defrost_derating | 48 | 1.357 | 1.354 | 1.225 | 1.225 | 9.7 | 0.00 | 0.00 | 2 | 1.2 | OK |
| defrost_derating | 96 | 2.703 | 2.696 | 2.302 | 2.280 | 14.8 | 0.97 | 0.91 | 3 | 4.8 | price: loppuehto 12 jakson lämmitysikkunalla |
| defrost_derating | 192 | 5.412 | 5.300 | 4.596 | 4.559 | 15.1 | 0.81 | 0.76 | 5 | 21.1 | OK |
| dst_autumn | 24 | 0.604 | 0.605 | 0.572 | 0.572 | 5.2 | 0.00 | 0.00 | 1 | 0.4 | OK |
| dst_autumn | 48 | 1.368 | 1.304 | 1.195 | 1.191 | 12.6 | 0.36 | 0.34 | 2 | 2.0 | OK |
| dst_autumn | 96 | 2.761 | 2.682 | 2.315 | 2.294 | 16.1 | 0.94 | 0.88 | 3 | 6.4 | OK |
| dst_autumn | 192 | 5.447 | 5.327 | 4.600 | 4.595 | 15.6 | 0.10 | 0.09 | 5 | 18.0 | OK |
| dst_spring | 24 | 0.604 | 0.605 | 0.572 | 0.572 | 5.2 | 0.00 | 0.00 | 1 | 0.5 | OK |
| dst_spring | 48 | 1.398 | 1.329 | 1.216 | 1.207 | 13.1 | 0.74 | 0.69 | 2 | 1.8 | OK |
| dst_spring | 96 | 2.750 | 2.682 | 2.270 | 2.269 | 17.5 | 0.02 | 0.02 | 3 | 4.2 | OK |
| dst_spring | 192 | 5.452 | 5.327 | 4.550 | 4.536 | 16.6 | 0.31 | 0.29 | 5 | 9.8 | OK |
| stale_weather_forecast | 24 | 0.604 | 0.605 | 0.642 | 0.572 | -6.4 | — | — | 1 | 0.4 | OK |
| stale_weather_forecast | 48 | 1.388 | 1.309 | 1.166 | 1.195 | — | — | — | 2 | 1.8 | mpc: loppuehto ei täyty (0.16 °C), NOT_COMPARABLE |
| stale_weather_forecast | 96 | 2.761 | 2.667 | 2.226 | 2.274 | — | — | — | 3 | 4.8 | mpc: loppuehto ei täyty (1.06 °C), NOT_COMPARABLE |
| stale_weather_forecast | 192 | 5.447 | 5.297 | 4.485 | 4.555 | — | — | — | 6 | 18.1 | mpc: 2 rajarikettä; mpc: loppuehto ei täyty (0.45 °C), NOT_COMPARABLE; MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 24 | 0.601 | 0.601 | 0.783 | 0.572 | -30.1 | — | — | 1 | 0.5 | OK |
| stale_weather_forecast_margin1 | 48 | 1.395 | 1.388 | 1.218 | 1.195 | 12.7 | — | — | 2 | 1.5 | MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 96 | 2.723 | 2.694 | 2.573 | 2.274 | 5.5 | — | — | 3 | 7.9 | MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 192 | 5.429 | 5.405 | 5.139 | 4.555 | 5.3 | — | — | 8 | 16.8 | price: loppuehto 12 jakson lämmitysikkunalla; MPC: FALLBACK_REPLAY_FAILED |

Ei taulukossa (katso C++-vikatestit):
- `supply_water_cop_penalty`: NOT_APPLICABLE: air-to-air pump has no supply water; covered by hydronic G0 (tools/reference_benchmark.py)
- `missing_price_slot`: fault path: BridgeStatus::Gap -> no MPC plan (C++ test)
- `sensor_outage`: fault path: bootstrap UNAVAILABLE, MPC not run (C++ test)
- `pump_link_outage`: fault path: control gate INPUTS_UNVERIFIED (C++ test)
- `optimizer_timeout`: fault path: MpcStatus::Timeout, count=0, no partial plan (C++ test)
- `no_feasible_plan`: fault path: UNSAFE_START / INFEASIBLE, no plan (C++ test)
