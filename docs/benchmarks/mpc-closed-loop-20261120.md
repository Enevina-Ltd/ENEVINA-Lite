| Scenario | Controller | Cost € | Equal-energy € | kWh | Final °C | Min °C | Below band K·h | Starts | MPC active / fallback |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| cold_spell | thermostat | 11.319 | 11.345 | 67.66 | 11.82 | 11.20 | 0.57 | 13 | — |
|  | lite_price | 10.837 | 10.891 | 67.62 | 11.63 | 11.13 | 0.65 | 17 | — |
|  | calibrated_mpc | 10.868 | 10.849 | 68.88 | 12.13 | 11.33 | 0.03 | 15 | 229/288 (most: OUTDOOR_OUTSIDE_LEARNED_RANGE 40), solver failures 15 {"INFEASIBLE":15} |
| fast_warmup | thermostat | 7.617 | 7.287 | 49.02 | 11.35 | 9.03 | 7.61 | 20 | — |
|  | lite_price | 7.271 | 6.852 | 49.61 | 11.99 | 9.03 | 7.62 | 23 | — |
|  | calibrated_mpc | 7.222 | 6.737 | 50.50 | 12.46 | 9.03 | 7.53 | 23 | 260/288 (most: BELOW_COMFORT_FLOOR 27) |
| price_spike | thermostat | 9.827 | 9.840 | 47.53 | 11.93 | 11.20 | 0.26 | 20 | — |
|  | lite_price | 9.071 | 9.114 | 47.78 | 11.77 | 11.24 | 0.14 | 26 | — |
|  | calibrated_mpc | 8.088 | 8.052 | 48.97 | 12.19 | 11.50 | 0.00 | 28 | 285/288 (most: ABOVE_COMFORT_CEILING 2) |
| cheap_night_expensive_morning | thermostat | 10.893 | 10.906 | 47.53 | 11.93 | 11.20 | 0.26 | 20 | — |
|  | lite_price | 10.504 | 10.463 | 47.81 | 12.22 | 11.31 | 0.10 | 22 | — |
|  | calibrated_mpc | 9.958 | 9.996 | 48.07 | 11.80 | 11.46 | 0.00 | 26 | 285/288 (most: BELOW_COMFORT_FLOOR 3) |
| negative_prices | thermostat | 5.891 | 5.898 | 47.53 | 11.93 | 11.20 | 0.26 | 20 | — |
|  | lite_price | 5.485 | 5.496 | 47.79 | 11.89 | 11.27 | 0.16 | 25 | — |
|  | calibrated_mpc | 5.123 | 5.074 | 48.98 | 12.46 | 11.47 | 0.00 | 25 | 285/288 (most: ABOVE_COMFORT_CEILING 2) |
| flat_prices | thermostat | 9.253 | 9.264 | 47.53 | 11.93 | 11.20 | 0.26 | 20 | — |
|  | lite_price | 9.253 | 9.264 | 47.53 | 11.93 | 11.20 | 0.26 | 20 | — |
|  | calibrated_mpc | 9.398 | 9.353 | 48.41 | 12.28 | 11.45 | 0.00 | 21 | 285/288 (most: BELOW_COMFORT_FLOOR 2), solver failures 1 {"REPLAY_FAILED":1} |
| undersized_pump | thermostat | 6.335 | 7.358 | 36.51 | 4.40 | 2.22 | 464.19 | 1 | — |
|  | lite_price | 6.352 | 7.375 | 36.89 | 4.41 | 2.23 | 456.58 | 1 | — |
|  | calibrated_mpc | 6.352 | 7.375 | 36.89 | 4.41 | 2.23 | 456.58 | 1 | 0/288 (most: MODEL_LEARNING 288) |
| sensor_outage | thermostat | 7.543 | 7.569 | 48.48 | 11.82 | 11.27 | 0.20 | 17 | — |
|  | lite_price | 7.328 | 7.324 | 49.12 | 12.03 | 11.30 | 0.06 | 22 | — |
|  | calibrated_mpc | 7.155 | 7.094 | 49.95 | 12.43 | 11.55 | 0.00 | 20 | 243/288 (most: TEMPERATURE_STALE 24) |
| weather_price_outage | thermostat | 7.360 | 7.370 | 47.53 | 11.93 | 11.20 | 0.26 | 20 | — |
|  | lite_price | 6.990 | 7.004 | 47.79 | 11.90 | 11.28 | 0.15 | 25 | — |
|  | calibrated_mpc | 6.888 | 6.849 | 48.69 | 12.28 | 11.38 | 0.01 | 26 | 190/288 (most: FORECAST_UNAVAILABLE 96), solver failures 1 {"INFEASIBLE":1} |
| wrong_initial_guess | thermostat | 7.763 | 7.830 | 49.74 | 11.51 | 11.30 | 0.16 | 20 | — |
|  | lite_price | 7.329 | 7.287 | 50.59 | 12.31 | 11.29 | 0.12 | 25 | — |
|  | calibrated_mpc | 7.234 | 7.177 | 51.52 | 12.42 | 11.48 | 0.00 | 24 | 285/288 (most: SOLVER_FAILED 2), solver failures 2 {"REPLAY_FAILED":2} |
| price_spike_pump_meter | thermostat | 9.827 | 9.840 | 47.53 | 11.93 | 11.20 | 0.26 | 20 | — |
|  | lite_price | 9.071 | 9.114 | 47.78 | 11.77 | 11.24 | 0.14 | 26 | — |
|  | calibrated_mpc | 8.084 | 8.046 | 48.97 | 12.20 | 11.49 | 0.00 | 28 | 287/288 (most: ABOVE_COMFORT_CEILING 1) |
