#pragma once
#include <WebServer.h>
#include "cottage/bounded_http_write.hpp"

// One Arduino WebServer, same routes and authorization. Override its virtual
// transport hooks rather than changing the pinned SDK or adding a watchdog.
// Streaming handlers use streamContent() so the chunk trailer cannot bypass
// the bounded transport. This does not replace the SDK's incoming parser.
class CottageWebServer final : public WebServer {
public:
    explicit CottageWebServer(int port):WebServer(port){}
    void handleClient() override;
    bool streamContent(const char* data);
    bool responseAborted()const{return aborted_;}
    uint32_t responseAborts()const{return aborts_;}
    uint32_t longestWriteMs()const{return longest_write_ms_;}
protected:
    size_t _currentClientWrite(const char*,size_t) override;
    size_t _currentClientWrite_P(PGM_P,size_t) override;
private:
    size_t writeBounded(const char*,size_t);
    void abortResponse();
    uint32_t start_ms_=0,aborts_=0,longest_write_ms_=0;
    bool aborted_=false;
};
