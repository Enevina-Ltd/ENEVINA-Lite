#pragma once
namespace enevina::cottage {
// Fail-closed endpoint allowlist. No credentials, fragments, query parameters,
// path substitutions, percent escaping, IPv6 literals or redirects.
// Only exact read-only LAN endpoints are accepted.
bool valid_cn105_events_url(const char* url);
bool valid_shelly_base_url(const char* url);
}
