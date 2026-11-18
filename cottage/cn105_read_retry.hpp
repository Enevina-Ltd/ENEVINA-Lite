#pragma once
// CN105 ESPHome read-only HTTP timeout recovery.
// Bounded retries, no write retries and no change to control safety gates.
namespace enevina::cottage {
constexpr unsigned cn105_read_max_attempts=2;
constexpr unsigned cn105_read_connect_timeout_ms=2500;
constexpr unsigned cn105_read_response_timeout_ms=4500;
constexpr unsigned cn105_read_retry_delay_ms=250;
constexpr int cn105_http_connection_lost=-5;
constexpr int cn105_http_read_timeout=-11;
constexpr bool cn105_read_retryable(int http_code) {
    return http_code==cn105_http_read_timeout ||
           http_code==cn105_http_connection_lost;
}
constexpr bool cn105_should_retry_read(int http_code,unsigned attempt) {
    return cn105_read_retryable(http_code) &&
           attempt+1<cn105_read_max_attempts;
}
static_assert(cn105_read_max_attempts==2,"Bound GET retries");
static_assert(cn105_read_response_timeout_ms<=5000,"Bound GET timeout");
} // namespace enevina::cottage
