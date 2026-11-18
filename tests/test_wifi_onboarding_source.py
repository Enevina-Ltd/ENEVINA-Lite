"""Source regressions for phase-1 Wi-Fi onboarding and phase-2 settings glue.

The state machine itself is host-tested in tests/wifi_provisioning_tests.cpp;
these checks pin the firmware wiring that a host test cannot execute.
None of this is a physical ESP32-S3 / phone test.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "cottage"
WIFI = (FW / "src" / "cottage_wifi.cpp").read_text(encoding="utf-8")
PAGE = (FW / "src" / "cottage_wifi_page.cpp").read_text(encoding="utf-8") \
    .split('R"HTML(', 1)[1].split(')HTML"', 1)[0]
MAIN = (FW / "src" / "main.cpp").read_text(encoding="utf-8")
SITE = (FW / "src" / "cottage_site_settings.cpp").read_text(encoding="utf-8")
AIR = (FW / "src" / "cottage_air_service.cpp").read_text(encoding="utf-8")
WIFI_HDR = (FW / "include" / "cottage_wifi.hpp").read_text(encoding="utf-8")
UI = (FW / "include" / "cottage_premium_dashboard.hpp").read_text(encoding="utf-8")


class WifiOnboardingSource(unittest.TestCase):
    def test_setup_ap_is_open_without_password(self):
        # Owner decision 18.11.2026: no setup-network password.
        self.assertIn("WiFi.softAP(ap_ssid,nullptr,1,0,2)", WIFI)
        self.assertNotIn("ap_password", WIFI)
        self.assertNotIn('"ap_pw"', WIFI)
        self.assertIn('"Enevina-Lite-%02X%02X"', WIFI)
        # Open AP only while needed: closes after the grace period.
        self.assertIn("else if(!machine.ap_should_run() && ap_active) stop_ap();", WIFI)

    def test_credentials_stored_only_after_verified_join(self):
        self.assertIn("WiFi.persistent(false)", WIFI)
        loop = WIFI.split("void cottage_wifi_loop()", 1)[1]
        self.assertIn("machine.take_commit() && !persist(machine.stored())", loop)
        connect = WIFI.split("void connect_route()", 1)[1].split("void captive_redirect()", 1)[0]
        self.assertNotIn("persist(", connect)

    def test_home_wifi_password_never_logged_or_returned(self):
        for line in WIFI.splitlines():
            if "Serial.print" in line:
                self.assertNotIn("stored().password", line)
                self.assertNotIn("candidate().password", line)
        status = WIFI.split("void status_route()", 1)[1].split("void scan_route()", 1)[0]
        self.assertNotIn("password", status)

    def test_existing_token_reused_no_new_gate(self):
        self.assertIn("c23_authorize_local_setting(server->arg(\"token\").c_str())", WIFI)
        self.assertIn("c23_authorize_local_setting(web.arg(\"token\").c_str())", SITE)
        self.assertNotRegex(WIFI + SITE, r"(?i)admin_password|login_required|pin_code")

    def test_phase_one_page_asks_only_for_wifi(self):
        for forbidden in ("CN105", "Shelly", "Ruuvi", "tariff", "siirto", "marginaali"):
            self.assertNotIn(forbidden, PAGE)
        self.assertIn("Vaihe 1/2", PAGE)
        self.assertIn("WRONG_PASSWORD", PAGE)
        self.assertIn("prefers-color-scheme:dark", PAGE)
        self.assertIn("url_numeric", PAGE)  # mDNS fallback always shown

    def test_ssid_submitted_verbatim(self):
        # Codex P2 (PR #30): leading/trailing spaces are part of an SSID.
        self.assertIn("const ssid=$('ssid').value,", PAGE)
        self.assertNotIn("$('ssid').value.trim()", PAGE)

    def test_main_wiring(self):
        self.assertIn("cottage_wifi_begin(web);", MAIN)
        self.assertIn("cottage_wifi_loop();", MAIN)
        self.assertIn("cottage_wifi_request_via_ap(web)", MAIN)
        self.assertNotIn("WiFi.begin(COTTAGE_WIFI_SSID", MAIN)
        self.assertLess(MAIN.index("c23_initialize();"), MAIN.index("cottage_wifi_begin(web);"))
        self.assertLess(MAIN.index("site_settings_initialize();"),
                        MAIN.index("enevina_firmware::start_weather_reader();"))

    def test_setup_ap_exposes_wifi_only_even_while_home_connected(self):
        # Codex P1 (PR #30): a public, intentionally passwordless setup AP
        # must never expose a physical pump command or phase-2 settings.
        self.assertIn("web.client().localIP()==WiFi.softAPIP()", WIFI)
        self.assertIn("bool cottage_wifi_require_home_lan(WebServer& web)", WIFI_HDR)
        self.assertIn("if(!cottage_wifi_request_via_ap(web)) return true;", WIFI)
        self.assertIn('web.send(403,"application/json"', WIFI)
        arm = MAIN.split('web.on("/api/control/arm",HTTP_POST', 1)[1]
        self.assertLess(arm.index("cottage_wifi_require_home_lan(web)"),
                        arm.index('web.hasArg("token")'))
        for endpoint in ("status", "plan", "prices"):
            self.assertIn('web.on("/api/'+endpoint+'",HTTP_GET,[](){', MAIN)
            self.assertIn("if(cottage_wifi_require_home_lan(web))"+
                          endpoint+"_endpoint();", MAIN)
        for endpoint in ("config", "thresholds", "bind", "ack"):
            handler = AIR.split('web.on("/api/air/'+endpoint+'",HTTP_POST', 1)[1]
            self.assertLess(handler.index("cottage_wifi_require_home_lan(web)"),
                            handler.index("check_token(web)"))
        self.assertIn("if(cottage_wifi_require_home_lan(web))c23_history_http(web);", AIR)
        self.assertIn("if(cottage_wifi_require_home_lan(web))send_settings(web);", SITE)
        settings_post = SITE.split('web.on("/api/settings",HTTP_POST', 1)[1]
        self.assertLess(settings_post.index("cottage_wifi_require_home_lan(web)"),
                        settings_post.index('web.hasArg("token")'))
        # Wi-Fi setup remains usable from the AP.
        self.assertIn('web.on("/api/wifi/connect",HTTP_POST,connect_route);', WIFI)
        self.assertIn('web.on("/api/wifi/status",HTTP_GET,status_route);', WIFI)

    def test_captive_probes_and_mdns_fallback(self):
        for probe in ("/generate_204", "/hotspot-detect.html", "/connecttest.txt"):
            self.assertIn(probe, WIFI)
        self.assertIn('MDNS.begin("enevina-lite")', WIFI)
        self.assertIn("WiFi.setHostname(unique_host)", WIFI)


class SiteSettingsSource(unittest.TestCase):
    def test_nvs_written_before_ram(self):
        post = SITE.split('web.on("/api/settings",HTTP_POST', 1)[1]
        self.assertLess(post.index("persist(next)"), post.index("current=next;"))

    def test_settings_change_cancels_inflight_command(self):
        post = MAIN.split("static bool cn105_bridge_post(", 1)[1].split(
            "static void cottage_control_tick()", 1)[0]
        self.assertEqual(post.count("site_settings_generation()!=site_generation"), 2)
        self.assertLess(post.index("site_settings_generation()!=site_generation"),
                        post.index('req.POST("")'))

    def test_endpoint_change_revokes_armed_control(self):
        # Codex P1 (PR #30): authorization is bound to the configured pump.
        self.assertIn("endpoint_changed) endpoint_generation.fetch_add(1)", SITE)
        self.assertIn("strcmp(current.cn105_url,next.cn105_url)!=0", SITE)
        tick = MAIN.split("static void cottage_control_tick() {", 1)[1].split(
            "ControlSnapshot candidate", 1)[0]
        self.assertIn("control_armed_endpoint.load()!=site_settings_endpoint_generation()", tick)
        self.assertIn("cottage_control_disarm();", tick)
        disarm = MAIN.split("static void cottage_control_disarm() {", 1)[1].split("}", 3)
        self.assertIn('prefs.putBool("armed",false)', "}".join(disarm))
        post = MAIN.split("static bool cn105_bridge_post(", 1)[1].split(
            "static void cottage_control_tick()", 1)[0]
        self.assertIn("control_armed_endpoint.load()!=site_settings_endpoint_generation()", post)
        arm = MAIN.split('web.on("/api/control/arm"', 1)[1]
        self.assertLess(arm.index("control_armed_endpoint.store("),
                        arm.index("control_armed.store(requested)"))

    def test_location_bounds_match_weather_reader(self):
        weather = (ROOT / "firmware" / "main" / "weather.cpp").read_text(encoding="utf-8")
        settings = (ROOT / "cottage" / "site_settings.hpp").read_text(encoding="utf-8")
        self.assertIn("latitude>=59 && latitude<=71", weather)
        self.assertIn("longitude>=19 && longitude<=32", weather)
        self.assertIn("y>=59&&y<=71&&x>=19&&x<=32", settings)

    def test_only_firmware_adapters_selectable(self):
        self.assertIn("adapter_selectable_in_lite_firmware(next.pump_adapter)", SITE)

    def test_dashboard_has_phase_two_cards(self):
        for element in ('id="site-card"', 'id="devices-card"', 'id="site-save"',
                        'id="adapter-rows"', 'href="/setup"'):
            self.assertIn(element, UI)
        self.assertIn("'/api/settings'", UI)
        # Adapter table cells are text, never HTML from the device.
        self.assertRegex(UI, re.compile(r"insertCell\(\)\.textContent=v"))


if __name__ == "__main__":
    unittest.main()
