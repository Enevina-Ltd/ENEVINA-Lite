| Testitapaus | Jaksot | Perusohjaus € | Hintaraja € | Enevina MPC € | MILP € | Säästö vs perus % | Ero MILP € % | Ero MILP tavoite % | MPC käynn. | MPC ms (x86) | Rajoitukset / huom. |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| base | 24 | 0.581 | 0.605 | 0.572 | 0.572 | 1.4 | 0.00 | 0.00 | 1 | 0.5 | thermostat: loppuvaje 0.15 °C tasattu |
| base | 48 | 1.344 | 1.299 | 1.195 | 1.195 | 11.0 | 0.00 | 0.00 | 2 | 2.1 | thermostat: loppuvaje 0.40 °C tasattu |
| base | 96 | 2.726 | 2.648 | 2.296 | 2.274 | 15.8 | 0.96 | 0.90 | 3 | 5.5 | thermostat: loppuvaje 0.08 °C tasattu |
| base | 192 | 5.428 | 5.259 | 4.559 | 4.555 | 16.0 | 0.10 | 0.09 | 5 | 26.7 | thermostat: loppuvaje 0.14 °C tasattu |
| flat_price | 24 | 0.789 | 0.818 | 0.771 | 0.771 | 2.3 | 0.00 | 0.00 | 1 | 0.5 | thermostat: loppuvaje 0.15 °C tasattu |
| flat_price | 48 | 1.548 | 1.539 | 1.484 | 1.484 | 4.1 | 0.00 | 0.00 | 2 | 2.1 | thermostat: loppuvaje 0.40 °C tasattu |
| flat_price | 96 | 2.951 | 2.925 | 2.817 | 2.815 | 4.6 | 0.04 | 0.04 | 3 | 14.1 | thermostat: loppuvaje 0.08 °C tasattu; MILP: aikaraja, ei todistettu optimi |
| flat_price | 192 | 5.880 | 5.850 | 5.527 | 5.659 | 6.0 | -2.33 | -3.14 | 7 | 10.2 | thermostat: loppuvaje 0.14 °C tasattu; MILP: aikaraja, ei todistettu optimi |
| price_spikes | 24 | 0.605 | 0.627 | 0.591 | 0.591 | 2.3 | 0.00 | 0.00 | 1 | 0.4 | thermostat: loppuvaje 0.15 °C tasattu |
| price_spikes | 48 | 3.522 | 2.525 | 1.154 | 1.154 | 67.2 | 0.00 | 0.00 | 2 | 1.5 | thermostat: loppuvaje 0.40 °C tasattu |
| price_spikes | 96 | 7.218 | 5.428 | 2.163 | 2.205 | 70.0 | -1.94 | 0.73 | 4 | 5.2 | thermostat: loppuvaje 0.08 °C tasattu; price: loppuvaje 0.26 °C tasattu |
| price_spikes | 192 | 14.372 | 11.703 | 4.411 | 4.489 | 69.3 | -1.72 | -0.36 | 6 | 10.5 | thermostat: loppuvaje 0.14 °C tasattu; price: loppuvaje 0.22 °C tasattu; MILP: aikaraja, ei todistettu optimi |
| negative_prices | 24 | 0.666 | 0.690 | 0.651 | 0.651 | 2.3 | 0.00 | 0.00 | 1 | 0.3 | thermostat: loppuvaje 0.15 °C tasattu |
| negative_prices | 48 | 1.132 | 1.043 | 0.905 | 0.905 | 20.1 | 0.00 | 0.00 | 2 | 1.4 | thermostat: loppuvaje 0.40 °C tasattu |
| negative_prices | 96 | 1.988 | 1.915 | 1.514 | 1.514 | 23.8 | 0.00 | 0.00 | 3 | 4.2 | thermostat: loppuvaje 0.08 °C tasattu |
| negative_prices | 192 | 4.044 | 3.885 | 3.035 | 3.029 | 25.0 | 0.17 | 0.16 | 6 | 15.9 | thermostat: loppuvaje 0.14 °C tasattu; MILP: aikaraja, ei todistettu optimi |
| expensive_night_cheap_day | 24 | 2.018 | 2.091 | 1.972 | 1.972 | 2.3 | 0.00 | 0.00 | 1 | 0.4 | thermostat: loppuvaje 0.15 °C tasattu |
| expensive_night_cheap_day | 48 | 2.710 | 2.514 | 2.145 | 2.145 | 20.8 | 0.00 | 0.00 | 3 | 1.5 | thermostat: loppuvaje 0.40 °C tasattu |
| expensive_night_cheap_day | 96 | 3.934 | 3.769 | 3.098 | 3.098 | 21.3 | 0.00 | 0.00 | 3 | 4.9 | thermostat: loppuvaje 0.08 °C tasattu; price: loppuvaje 0.32 °C tasattu |
| expensive_night_cheap_day | 192 | 8.223 | 7.691 | 5.817 | 5.820 | 29.3 | -0.06 | -1.01 | 6 | 10.1 | thermostat: loppuvaje 0.14 °C tasattu; price: loppuvaje 0.28 °C tasattu; MILP: aikaraja, ei todistettu optimi |
| cheap_night_expensive_day | 24 | 0.359 | 0.372 | 0.351 | 0.351 | 2.3 | 0.00 | 0.00 | 1 | 0.5 | thermostat: loppuvaje 0.15 °C tasattu |
| cheap_night_expensive_day | 48 | 1.891 | 1.747 | 1.208 | 1.208 | 36.1 | 0.00 | 0.00 | 2 | 1.9 | thermostat: loppuvaje 0.40 °C tasattu |
| cheap_night_expensive_day | 96 | 4.773 | 4.368 | 3.401 | 3.399 | 28.8 | 0.05 | 0.05 | 4 | 6.0 | thermostat: loppuvaje 0.08 °C tasattu |
| cheap_night_expensive_day | 192 | 9.146 | 8.736 | 6.679 | 6.682 | 27.0 | -0.04 | -0.04 | 7 | 9.9 | thermostat: loppuvaje 0.14 °C tasattu; MILP: aikaraja, ei todistettu optimi |
| hard_frost | 24 | 1.181 | 1.199 | 1.203 | 1.203 | -1.9 | 0.00 | 0.00 | 1 | 0.2 | thermostat: loppuvaje 0.76 °C tasattu |
| hard_frost | 48 | 2.765 | 2.733 | 2.672 | 2.652 | 3.4 | 0.78 | 0.75 | 2 | 0.8 | thermostat: loppuvaje 0.36 °C tasattu; price: loppuvaje 0.79 °C tasattu |
| hard_frost | 96 | 5.745 | 5.623 | 5.296 | 5.296 | 7.8 | 0.00 | 0.00 | 3 | 3.2 | thermostat: loppuvaje 0.74 °C tasattu; price: loppuvaje 1.11 °C tasattu |
| hard_frost | 192 | 11.509 | 11.180 | 10.538 | 10.538 | 8.4 | 0.00 | 0.00 | 5 | 13.6 | thermostat: loppuvaje 0.93 °C tasattu; price: loppuvaje 1.21 °C tasattu |
| mild | 24 | 0.267 | 0.269 | 0.176 | 0.176 | 34.1 | 0.00 | 0.00 | 1 | 0.5 | OK |
| mild | 48 | 0.462 | 0.462 | 0.292 | 0.288 | 36.7 | 1.38 | 1.05 | 2 | 1.8 | OK |
| mild | 96 | 0.671 | 0.705 | 0.383 | 0.379 | 42.9 | 1.13 | 0.92 | 2 | 4.8 | OK |
| mild | 192 | 1.316 | 1.377 | 0.794 | 0.785 | 39.7 | 1.18 | 0.99 | 3 | 9.4 | MILP: aikaraja, ei todistettu optimi |
| fast_outdoor_drop | 24 | 0.269 | 0.303 | 0.245 | 0.245 | 8.8 | 0.00 | 0.00 | 1 | 0.5 | OK |
| fast_outdoor_drop | 48 | 0.674 | 0.719 | 0.510 | 0.507 | 24.4 | 0.44 | 0.37 | 2 | 1.8 | OK |
| fast_outdoor_drop | 96 | 3.200 | 3.041 | 3.244 | 3.219 | NOT_COMPARABLE | 0.78 | 0.76 | 2 | 4.1 | thermostat: 1 rajarikettä; thermostat: loppuvaje 2.02 °C tasattu; price: 15 rajarikettä; price: loppuvaje 2.75 °C tasattu |
| fast_outdoor_drop | 192 | 8.577 | 8.351 | — | — | NOT_COMPARABLE | — | — | — | 7.0 | thermostat: 97 rajarikettä; thermostat: loppuvaje 4.93 °C tasattu; price: 111 rajarikettä; price: loppuvaje 5.10 °C tasattu; mpc: INFEASIBLE; MILP: ei ratkaisua (status 2) |
| slow_building | 24 | 0.570 | 0.572 | 0.502 | 0.502 | 11.9 | 0.00 | 0.00 | 1 | 0.4 | OK |
| slow_building | 48 | 1.203 | 1.104 | 0.984 | 0.984 | 18.2 | 0.00 | 0.00 | 1 | 1.0 | OK |
| slow_building | 96 | 2.296 | 1.939 | 1.894 | 1.858 | 17.5 | 1.96 | 1.81 | 3 | 3.9 | price: loppuvaje 0.01 °C tasattu |
| slow_building | 192 | 4.490 | 4.071 | 3.633 | 3.533 | 19.1 | 2.84 | 1.06 | 4 | 10.3 | OK |
| fast_cooling_building | 24 | 0.762 | 0.787 | 0.838 | 0.838 | NOT_COMPARABLE | 0.00 | 0.00 | 2 | 0.1 | thermostat: 6 rajarikettä; thermostat: loppuvaje 3.01 °C tasattu; price: 4 rajarikettä; price: loppuvaje 1.13 °C tasattu |
| fast_cooling_building | 48 | 1.751 | 1.790 | 1.891 | 1.891 | NOT_COMPARABLE | 0.00 | 0.00 | 3 | 0.8 | thermostat: 12 rajarikettä; thermostat: loppuvaje 2.01 °C tasattu; price: 7 rajarikettä; price: loppuvaje 1.52 °C tasattu |
| fast_cooling_building | 96 | 3.506 | 3.536 | 3.697 | 3.736 | NOT_COMPARABLE | -1.02 | -0.93 | 6 | 2.9 | thermostat: 21 rajarikettä; thermostat: loppuvaje 2.05 °C tasattu; price: 12 rajarikettä; price: loppuvaje 1.94 °C tasattu; MILP: aikaraja, ei todistettu optimi |
| fast_cooling_building | 192 | 6.990 | 6.995 | 7.454 | 7.833 | NOT_COMPARABLE | -4.84 | -4.12 | 11 | 6.2 | thermostat: 40 rajarikettä; thermostat: loppuvaje 2.60 °C tasattu; price: 27 rajarikettä; MILP: aikaraja, ei todistettu optimi |
| oversized_pump | 24 | 0.696 | 0.621 | 0.571 | 0.571 | 18.0 | 0.01 | 0.01 | 2 | 0.5 | OK |
| oversized_pump | 48 | 1.418 | 1.378 | 1.179 | 1.179 | 16.8 | 0.00 | 0.00 | 2 | 1.8 | OK |
| oversized_pump | 96 | 2.896 | 2.795 | 2.326 | 2.326 | 19.7 | 0.00 | 0.00 | 3 | 4.3 | OK |
| oversized_pump | 192 | 5.751 | 5.506 | 4.646 | 4.692 | 19.2 | -0.98 | -0.91 | 6 | 9.8 | MILP: aikaraja, ei todistettu optimi |
| undersized_pump | 24 | 0.501 | 0.526 | 0.501 | 0.501 | 0.0 | 0.00 | 0.00 | 1 | 0.5 | thermostat: loppuvaje 0.57 °C tasattu |
| undersized_pump | 48 | 1.194 | 1.119 | 1.139 | 1.125 | 4.6 | 1.23 | 1.14 | 2 | 1.4 | thermostat: loppuvaje 0.15 °C tasattu; price: loppuvaje 0.65 °C tasattu |
| undersized_pump | 96 | 2.483 | 2.388 | 2.237 | 2.207 | 9.9 | 1.39 | 1.30 | 3 | 3.7 | thermostat: loppuvaje 0.10 °C tasattu; price: loppuvaje 0.80 °C tasattu |
| undersized_pump | 192 | 4.950 | 4.750 | 4.309 | 4.293 | 12.9 | 0.37 | 0.35 | 5 | 8.2 | price: loppuvaje 0.76 °C tasattu |
| poor_cold_cop | 24 | 2.040 | 2.045 | 2.053 | 2.053 | -0.6 | 0.00 | 0.00 | 1 | 0.2 | price: loppuvaje 0.51 °C tasattu |
| poor_cold_cop | 48 | 4.352 | 4.431 | 4.017 | 4.017 | 7.7 | 0.00 | 0.00 | 2 | 1.1 | thermostat: loppuvaje 0.12 °C tasattu; price: loppuvaje 0.20 °C tasattu |
| poor_cold_cop | 96 | 8.171 | 8.145 | 7.406 | 7.318 | 9.4 | 1.21 | 1.18 | 3 | 3.9 | thermostat: loppuvaje 1.03 °C tasattu |
| poor_cold_cop | 192 | 16.250 | 16.305 | 14.641 | 14.591 | 9.9 | 0.34 | 0.33 | 5 | 8.9 | thermostat: loppuvaje 0.05 °C tasattu |
| defrost_derating | 24 | 0.595 | 0.576 | 0.581 | 0.581 | 2.2 | 0.00 | 0.00 | 1 | 0.3 | price: loppuvaje 0.42 °C tasattu |
| defrost_derating | 48 | 1.357 | 1.334 | 1.225 | 1.225 | 9.7 | 0.00 | 0.00 | 2 | 1.3 | price: loppuvaje 0.30 °C tasattu |
| defrost_derating | 96 | 2.703 | 2.647 | 2.302 | 2.280 | 14.8 | 0.97 | 0.91 | 3 | 6.5 | price: loppuvaje 0.13 °C tasattu |
| defrost_derating | 192 | 5.399 | 5.260 | 4.596 | 4.559 | 14.9 | 0.81 | 0.76 | 5 | 10.8 | thermostat: loppuvaje 0.47 °C tasattu |
| dst_autumn | 24 | 0.581 | 0.605 | 0.572 | 0.572 | 1.4 | 0.00 | 0.00 | 1 | 0.4 | thermostat: loppuvaje 0.15 °C tasattu |
| dst_autumn | 48 | 1.323 | 1.294 | 1.195 | 1.191 | 9.6 | 0.36 | 0.34 | 2 | 1.4 | thermostat: loppuvaje 0.40 °C tasattu |
| dst_autumn | 96 | 2.706 | 2.663 | 2.315 | 2.294 | 14.4 | 0.94 | 0.88 | 3 | 6.9 | thermostat: loppuvaje 0.08 °C tasattu |
| dst_autumn | 192 | 5.428 | 5.279 | 4.600 | 4.595 | 15.3 | 0.10 | 0.09 | 5 | 11.9 | thermostat: loppuvaje 0.14 °C tasattu |
| dst_spring | 24 | 0.581 | 0.605 | 0.572 | 0.572 | 1.4 | 0.00 | 0.00 | 1 | 0.8 | thermostat: loppuvaje 0.15 °C tasattu |
| dst_spring | 48 | 1.354 | 1.320 | 1.216 | 1.207 | 10.2 | 0.74 | 0.69 | 2 | 1.9 | thermostat: loppuvaje 0.40 °C tasattu |
| dst_spring | 96 | 2.716 | 2.658 | 2.270 | 2.269 | 16.4 | 0.02 | 0.02 | 3 | 4.4 | thermostat: loppuvaje 0.08 °C tasattu |
| dst_spring | 192 | 5.433 | 5.280 | 4.550 | 4.536 | 16.3 | 0.31 | 0.29 | 5 | 13.8 | thermostat: loppuvaje 0.14 °C tasattu |
| stale_weather_forecast | 24 | 0.581 | 0.605 | 0.642 | 0.572 | -10.6 | — | — | 1 | 0.4 | thermostat: loppuvaje 0.15 °C tasattu |
| stale_weather_forecast | 48 | 1.344 | 1.299 | 1.184 | 1.195 | 11.9 | — | — | 2 | 1.9 | thermostat: loppuvaje 0.40 °C tasattu; mpc: loppuvaje 0.16 °C tasattu |
| stale_weather_forecast | 96 | 2.726 | 2.648 | 2.356 | 2.274 | 13.6 | — | — | 3 | 10.3 | thermostat: loppuvaje 0.08 °C tasattu; mpc: loppuvaje 1.06 °C tasattu |
| stale_weather_forecast | 192 | 5.428 | 5.259 | — | 4.555 | — | — | — | 6 | 16.3 | thermostat: loppuvaje 0.14 °C tasattu; mpc: 2 rajarikettä; mpc: loppuvaje 0.45 °C tasattu; MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 24 | 0.584 | 0.585 | 0.783 | 0.572 | -34.1 | — | — | 1 | 0.4 | thermostat: loppuvaje 0.60 °C tasattu; price: loppuvaje 0.23 °C tasattu |
| stale_weather_forecast_margin1 | 48 | 1.338 | 1.360 | 1.218 | 1.195 | 8.9 | — | — | 2 | 3.3 | thermostat: loppuvaje 0.03 °C tasattu; price: loppuvaje 0.18 °C tasattu; MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 96 | 2.723 | 2.694 | 2.573 | 2.274 | 5.5 | — | — | 3 | 3.9 | MPC: FALLBACK_UNSAFE_START |
| stale_weather_forecast_margin1 | 192 | 5.426 | 5.348 | 5.139 | 4.555 | 5.3 | — | — | 8 | 13.0 | thermostat: loppuvaje 0.58 °C tasattu; price: loppuvaje 0.06 °C tasattu; MPC: FALLBACK_REPLAY_FAILED |

Ei taulukossa (katso C++-vikatestit):
- `supply_water_cop_penalty`: NOT_APPLICABLE: air-to-air pump has no supply water; covered by hydronic G0 (tools/reference_benchmark.py)
- `missing_price_slot`: fault path: BridgeStatus::Gap -> no MPC plan (C++ test)
- `sensor_outage`: fault path: bootstrap UNAVAILABLE, MPC not run (C++ test)
- `pump_link_outage`: fault path: control gate INPUTS_UNVERIFIED (C++ test)
- `optimizer_timeout`: fault path: MpcStatus::Timeout, count=0, no partial plan (C++ test)
- `no_feasible_plan`: fault path: UNSAFE_START / INFEASIBLE, no plan (C++ test)
