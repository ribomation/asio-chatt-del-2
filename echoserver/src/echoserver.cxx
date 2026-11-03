// Echo-server, asynkron med callbacks: alla klienter i en enda tråd.
//
//   async_accept  -> handler skapar en Session och startar nästa accept
//   async_read_some -> handler startar async_write av det som kom
//   async_write   -> handler startar nästa async_read_some
//
// Varje Session håller sig själv vid liv via super::shared_from_this() så länge
// en operation är utestående. När klienten kopplar ned (eof) startas ingen ny
// operation, sista handlern släpper sin shared_ptr, och sessionen destrueras.
//
// Testa med flera samtidiga: nc localhost 7000
#include <boost/asio.hpp>
#include <array>
#include <cstdio>
#include <exception>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <utility>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

auto to_string(tcp::endpoint const& ep) -> std::string {
    return std::format("{}:{}", ep.address().to_string(), ep.port());
}

class Session : public std::enable_shared_from_this<Session> {
    using super = std::enable_shared_from_this<Session>;

    tcp::socket socket;
    std::string peer;
    std::array<char, 1024> buf{};  // medlem, inte lokal: måste leva tills handlern körs

public:
    explicit Session(tcp::socket sock)
        : socket{std::move(sock)}, peer{to_string(socket.remote_endpoint())} {}

    ~Session() { std::println("nedkopplad: {}", peer); }

    void start() {
        std::println("ansluten: {}", peer);
        do_read();
    }

private:
    void do_read() {
        auto self = super::shared_from_this();
        socket.async_read_some(asio::buffer(buf), [self](boost::system::error_code ec, std::size_t n) {
            if (ec) {
                if (ec != asio::error::eof) std::println("fel mot {}: {}", self->peer, ec.message());
                return;  // ingen ny operation: sessionen dör när self går ur scope
            }
            self->do_write(n);
        });
    }

    void do_write(std::size_t n) {
        auto self = super::shared_from_this();
        asio::async_write(socket, asio::buffer(buf, n), [self](boost::system::error_code ec, std::size_t) {
            if (ec) {
                std::println("fel mot {}: {}", self->peer, ec.message());
                return;
            }
            self->do_read();
        });
    }
};

// Accept-loopen: varje accepterad klient blir en Session, och sedan startas
// nästa accept. Så länge en accept är utestående har run() något att vänta på.
void do_accept(tcp::acceptor& acceptor) {
    acceptor.async_accept([&acceptor](boost::system::error_code ec, tcp::socket sock) {
        if (ec) {
            std::println("accept: {}", ec.message());
            return;
        }
        std::make_shared<Session>(std::move(sock))->start();
        do_accept(acceptor);
    });
}

int main(int argc, char** argv) try {
    auto port = static_cast<unsigned short>(argc > 1 ? std::stoi(argv[1]) : 7000);

    auto ioc = asio::io_context{};
    auto acceptor = tcp::acceptor{ioc, tcp::endpoint{tcp::v4(), port}};
    std::println("echo-server (callbacks) lyssnar på port {}", port);

    do_accept(acceptor);
    ioc.run();  // kör alla handlers, i den här tråden, tills det inte finns mer arbete
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
