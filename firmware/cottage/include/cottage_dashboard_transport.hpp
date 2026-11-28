#pragma once
class CottageWebServer;
// Both gzip-capable and identity-only clients receive the exact same HTML.
void cottage_send_dashboard(CottageWebServer& web);
