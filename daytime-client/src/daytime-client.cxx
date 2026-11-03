// Daytime-klient, asynkron med callbacks: samma tre steg som den synkrona
// klienten i del 1 (resolve, connect, read), men som tre nästlade lambdas.
//
// Varje handler måste kontrollera felet själv, nästa steg ligger inne i
// handlern för det föregående, och ett fel kan inte bli ett returvärde från
// main utan en omväg. Det här är callback-helvetet i sin mildaste form, och
// det som del 3 skriver om med co_await.
//
// Servern är daytime-server från del 1, eller: while true; do date | nc -l -q 0 1313; done
#include <boost/asio.hpp>
#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <string>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

auto to_string(tcp::endpoint const& ep) -> std::string {
    return std::format("{}:{}", ep.address().to_string(), ep.port());
}

int main(int argc, char** argv) try {
    auto host = std::string{argc > 1 ? argv[1] : "localhost"};
    auto port = std::string{argc > 2 ? argv[2] : "1313"};

    auto ioc = asio::io_context{};
    auto resolver = tcp::resolver{ioc};
    auto sock = tcp::socket{ioc};
    auto reply = std::string{};
    auto status = 0;  // sätts av en handler vid fel, eftersom en lambda inte kan returnera från main

    // Lokala variabler fångade med & fungerar här, eftersom main lever tills
    // run() returnerar. I en server med många sessioner går det inte, och
    // därför har echo-servern en Session-klass som äger sig själv.

    resolver.async_resolve(host, port, [&](boost::system::error_code ec, tcp::resolver::results_type endpoints) {
        if (ec) {
            std::println(stderr, "fel: resolve: {}", ec.message());
            status = 1;
            return;
        }

        asio::async_connect(sock, endpoints, [&](boost::system::error_code ec, tcp::endpoint ep) {
            if (ec) {
                std::println(stderr, "fel: connect: {}", ec.message());
                status = 1;
                return;
            }
            std::println("ansluten till {}", to_string(ep));

            // Läs tills servern stänger. eof är det förväntade slutet, inte ett fel.
            asio::async_read(sock, asio::dynamic_buffer(reply), [&](boost::system::error_code ec, std::size_t) {
                if (ec && ec != asio::error::eof) {
                    std::println(stderr, "fel: read: {}", ec.message());
                    status = 1;
                    return;
                }
                std::print("svar: {}", reply);
            });
        });
    });

    ioc.run();  // alla tre stegen körs här, i tur och ordning
    return status;
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
