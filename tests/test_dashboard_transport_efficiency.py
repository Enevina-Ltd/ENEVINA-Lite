"""Read-only presentation/memory source regressions, plus exact gzip payload."""
import gzip
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/"firmware/cottage"
UI=(FW/"include/cottage_premium_dashboard.hpp").read_text()
MAIN=(FW/"src/main.cpp").read_text()
TRANSPORT=(FW/"src/cottage_dashboard_transport.cpp").read_text()
MARKET=(ROOT/"firmware/main/market.cpp").read_text()
WEATHER=(ROOT/"firmware/main/weather.cpp").read_text()
SPEC=importlib.util.spec_from_file_location("dash_build",FW/"cottage_dashboard_build.py")
MOD=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)

class DashboardTransportEfficiency(unittest.TestCase):
    def test_exact_static_compression(self):
        source=FW/"include/cottage_premium_dashboard.hpp"
        data=MOD.dashboard_gzip(source)
        self.assertEqual(gzip.decompress(data),MOD.dashboard_bytes(source))
        self.assertEqual(data,MOD.dashboard_gzip(source))
        self.assertLess(len(data),len(MOD.dashboard_bytes(source))*0.4)
        self.assertIn('web.sendHeader("Vary","Accept-Encoding");',TRANSPORT)
        self.assertIn('if(web.acceptsGzip())',TRANSPORT)
        self.assertIn('sizeof(premium_dashboard_gzip)',TRANSPORT)
        self.assertIn('cottage_send_dashboard(web)',MAIN)
        self.assertNotIn('premium_dashboard_html',MAIN+TRANSPORT)
        self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',TRANSPORT)
        self.assertIn('TINFL_LZ_DICT_SIZE',TRANSPORT)
        self.assertIn('TINFL_STATUS_DONE',TRANSPORT)
        self.assertIn('mz_crc32',TRANSPORT)
        self.assertIn('web.abortStream()',TRANSPORT)
        self.assertIn('web.streamContent(reinterpret_cast<const char*>(dict.get()),out)',TRANSPORT)
    def test_polling_is_serial_and_selected(self):
        poll=UI.split("async function periodicRefresh(){",1)[1].split("</script>",1)[0]
        self.assertIn("state.periodicBusy||document.hidden===true",poll)
        self.assertIn("finally{state.periodicBusy=false;}",poll)
        self.assertIn("await reloadStatus();",poll)
        self.assertIn("await reloadSelectedPump();",poll)
        self.assertIn("setInterval(periodicRefresh,15000);",poll)
        self.assertNotIn("setInterval(reloadMidea",UI)
        self.assertNotIn("setInterval(load,60000)",UI)
        pump=UI.split("async function reloadSelectedPump(){",1)[1].split("async function initialRefresh",1)[0]
        self.assertIn("state.site?.pump_adapter==='midea_lan'&&!wzMideaTimer",pump)
        self.assertIn("await reloadGree()",pump)
        self.assertNotIn("Promise.all",pump)
    def test_reclaim_unused_internal_plan_and_external_forecast_storage(self):
        self.assertNotIn("static std::array<Row,max_horizon_slots>",MAIN)
        self.assertIn("ForecastBuffer<PriceCache> cache;",MARKET)
        self.assertIn("auto fresh=forecast_buffer<PriceCache>();",MARKET)
        self.assertIn("auto saved=forecast_buffer<PriceCache>();",MARKET)
        self.assertNotIn("new(std::nothrow) enevina::PriceSlot[max_slots]",MARKET)
        self.assertIn("forecast_buffer<WeatherCache>()",WEATHER)
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT",(ROOT/"firmware/main/forecast_memory.hpp").read_text())
        self.assertIn("future_prices(prices,200,&ms)",MAIN)
        self.assertIn("future_prices(points,kMax,&market)",MAIN)
        self.assertIn("future_prices(points,kMaxPriceSlots,&market)",MAIN)

if __name__=="__main__":unittest.main()
