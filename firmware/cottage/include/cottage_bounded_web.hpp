#pragma once
#include <WebServer.h>
#include <array>
#include "cottage/bounded_http_write.hpp"
#include "cottage/bounded_http_request.hpp"

// One Arduino WebServer, same routes and authorization. Override its virtual
// transport hooks rather than changing the pinned SDK or adding a watchdog.
// Streaming handlers use streamContent() so the chunk trailer cannot bypass
// the bounded transport. Incremental ingress uses the same SDK route dispatch.
class CottageWebServer final : public WebServer {
public:
    explicit CottageWebServer(int port):WebServer(port){}
    ~CottageWebServer() override {finishAllRequests();}
    void handleClient() override;
    void close() override;
    uint32_t requestRejects()const{return request_rejects_;}
    uint32_t requestTimeouts()const{return request_timeouts_;}
    uint32_t idleTimeouts()const{return idle_timeouts_;}
    uint32_t partialTimeouts()const{return partial_timeouts_;}
    uint32_t idleCloses()const{return idle_closes_;}
    uint32_t idleEvictions()const{return idle_evictions_;}
    uint32_t partialCloses()const{return partial_closes_;}
    uint32_t invalidRequests()const{return invalid_requests_;}
    uint32_t resourceRejects()const{return resource_rejects_;}
    uint32_t completedRequests()const{return completed_requests_;}
    bool streamContent(const char* data);
    bool responseAborted()const{return aborted_;}
    bool acceptsGzip()const {
        return enevina::cottage::accepts_gzip(ingress_.head().headers);
    }
    uint32_t responseAborts()const{return aborts_;}
    uint32_t longestWriteMs()const{return longest_write_ms_;}
protected:
    size_t _currentClientWrite(const char*,size_t) override;
    size_t _currentClientWrite_P(PGM_P,size_t) override;
private:
    size_t writeBounded(const char*,size_t);
    void abortResponse();
    struct Pending {
        WiFiClient client;
        enevina::cottage::HttpIngress ingress{};
        char* buffer=nullptr;
        uint32_t started=0;
    };
    void finishRequest();
    void finishAllRequests();
    void closePending(Pending&);
    void rejectPending(Pending&,const char* status);
    void rejectRequest(const char* status);
    bool pollPending(Pending&);
    std::array<Pending,enevina::cottage::web_pending_connections> pending_{};
    size_t next_slot_=0;
    bool prepareRequest();
    enevina::cottage::HttpIngress ingress_{};
    char* request_buffer_=nullptr;
    uint32_t request_rejects_=0,request_timeouts_=0;
    uint32_t idle_timeouts_=0,partial_timeouts_=0,idle_closes_=0,idle_evictions_=0;
    uint32_t partial_closes_=0,invalid_requests_=0,resource_rejects_=0,completed_requests_=0;
    uint32_t start_ms_=0,aborts_=0,longest_write_ms_=0;
    bool aborted_=false;
};
