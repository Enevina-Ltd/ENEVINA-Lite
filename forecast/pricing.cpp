#include "forecast/pricing.hpp"
#include <cmath>

namespace enevina {
bool calculate_total_price(const Tariff& t, double& output) {
    if (!std::isfinite(t.spot_eur_per_kwh) ||
        !std::isfinite(t.margin_eur_per_kwh) ||
        !std::isfinite(t.transfer_eur_per_kwh) ||
        !std::isfinite(t.electricity_tax_eur_per_kwh) ||
        !std::isfinite(t.vat_rate) || t.vat_rate < 0 || t.vat_rate > 1 ||
        t.margin_eur_per_kwh < 0 || t.transfer_eur_per_kwh < 0 ||
        t.electricity_tax_eur_per_kwh < 0) return false;
    const auto gross = [rate=t.vat_rate](double value, bool includes_vat) {
        return value * (includes_vat ? 1 : 1 + rate);
    };
    const double total=gross(t.spot_eur_per_kwh,t.spot_includes_vat) +
        gross(t.margin_eur_per_kwh,t.margin_includes_vat) +
        gross(t.transfer_eur_per_kwh,t.transfer_includes_vat) +
        gross(t.electricity_tax_eur_per_kwh,t.electricity_tax_includes_vat);
    if (!std::isfinite(total)) return false;
    output=total;
    return true;
}
}
