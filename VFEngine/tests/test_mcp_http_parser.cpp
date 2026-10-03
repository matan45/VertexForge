#include <doctest.h>

#include "transport/HttpMessage.hpp"

using mcp::http::HttpRequestParser;
using State = HttpRequestParser::State;

TEST_CASE("mcp http: request split across packets")
{
    HttpRequestParser parser;
    const std::string raw =
        "POST /mcp HTTP/1.1\r\n"
        "Host: 127.0.0.1:7878\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"a\":\"hello\"}";

    for (std::size_t split = 1; split < raw.size(); split += 7)
    {
        HttpRequestParser p;
        p.feed(std::string_view(raw).substr(0, split));
        State first = p.next();
        if (first == State::Complete)
        {
            FAIL("completed before all bytes arrived at split " << split);
        }
        CHECK(first == State::NeedMore);
        p.feed(std::string_view(raw).substr(split));
        REQUIRE(p.next() == State::Complete);
        auto request = p.take();
        CHECK(request.method == "POST");
        CHECK(request.path() == "/mcp");
        CHECK(request.body == "{\"a\":\"hello\"}");
    }
}

TEST_CASE("mcp http: headers are case-insensitive and keep-alive defaults on")
{
    HttpRequestParser parser;
    parser.feed("POST /mcp?x=1 HTTP/1.1\r\nHOST: localhost:7878\r\nContent-length: 2\r\n\r\n{}");
    REQUIRE(parser.next() == State::Complete);
    auto request = parser.take();
    CHECK(request.header("host").value() == "localhost:7878");
    CHECK(request.header("Content-Length").value() == "2");
    CHECK(request.path() == "/mcp");
    CHECK(request.keepAlive());

    parser.feed("GET /mcp HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    REQUIRE(parser.next() == State::Complete);
    CHECK_FALSE(parser.take().keepAlive());

    parser.feed("GET /mcp HTTP/1.0\r\nHost: x\r\n\r\n");
    REQUIRE(parser.next() == State::Complete);
    CHECK_FALSE(parser.take().keepAlive());
}

TEST_CASE("mcp http: pipelined requests parse one at a time")
{
    HttpRequestParser parser;
    parser.feed("POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: 1\r\n\r\nA"
                "POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: 1\r\n\r\nB");
    REQUIRE(parser.next() == State::Complete);
    CHECK(parser.take().body == "A");
    REQUIRE(parser.next() == State::Complete);
    CHECK(parser.take().body == "B");
    CHECK(parser.next() == State::NeedMore);
    CHECK(parser.bufferedBytes() == 0);
}

TEST_CASE("mcp http: request without body has empty body")
{
    HttpRequestParser parser;
    parser.feed("DELETE /mcp HTTP/1.1\r\nHost: h\r\n\r\n");
    REQUIRE(parser.next() == State::Complete);
    CHECK(parser.take().body.empty());
}

TEST_CASE("mcp http: errors")
{
    SUBCASE("oversize body is 413")
    {
        HttpRequestParser parser(16);
        parser.feed("POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: 17\r\n\r\n");
        CHECK(parser.next() == State::Error);
        CHECK(parser.errorStatus() == 413);
    }
    SUBCASE("bad content length is 400")
    {
        HttpRequestParser parser;
        parser.feed("POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: 12abc\r\n\r\n");
        CHECK(parser.next() == State::Error);
        CHECK(parser.errorStatus() == 400);
    }
    SUBCASE("chunked is 501")
    {
        HttpRequestParser parser;
        parser.feed("POST /mcp HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\n");
        CHECK(parser.next() == State::Error);
        CHECK(parser.errorStatus() == 501);
    }
    SUBCASE("malformed request line is 400")
    {
        HttpRequestParser parser;
        parser.feed("GARBAGE\r\n\r\n");
        CHECK(parser.next() == State::Error);
        CHECK(parser.errorStatus() == 400);
    }
    SUBCASE("oversize headers are 431")
    {
        HttpRequestParser parser(1024, 32);
        parser.feed("POST /mcp HTTP/1.1\r\nHost: a-very-long-host-name-exceeding-limit\r\n");
        CHECK(parser.next() == State::Error);
        CHECK(parser.errorStatus() == 431);
    }
}

TEST_CASE("mcp http: response serialization")
{
    auto ok = mcp::http::HttpResponse::json(200, "{}");
    std::string wire = ok.serialize(true);
    CHECK(wire.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(wire.find("Content-Type: application/json\r\n") != std::string::npos);
    CHECK(wire.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(wire.find("Connection: keep-alive\r\n") != std::string::npos);
    CHECK(wire.substr(wire.size() - 6) == "\r\n\r\n{}");

    std::string noContent = mcp::http::HttpResponse::empty(204).serialize(false);
    CHECK(noContent.find("Content-Length") == std::string::npos);
    CHECK(noContent.find("Connection: close\r\n") != std::string::npos);

    auto replaced = mcp::http::HttpResponse::json(200, "{}");
    replaced.setHeader("content-type", "text/plain");
    CHECK(replaced.headers.size() == 1);
    CHECK(replaced.headers[0].second == "text/plain");
}
