#pragma once

namespace enevina {
// Each component is EUR/kWh. Fixed monthly charges are deliberately excluded
// because they cannot change a 15-minute dispatch decision.
struct Tariff {
    double spot_eur_per_kwh = 0;
    double margin_eur_per_kwh = 0;
    double transfer_eur_per_kwh = 0;
    double electricity_tax_eur_per_kwh = 0;
    double vat_rate = 0;
    bool spot_includes_vat = false;
    bool margin_includes_vat = false;
    bool transfer_includes_vat = false;
    bool electricity_tax_includes_vat = false;
};
bool calculate_total_price(const Tariff& tariff, double& eur_per_kwh);
}
