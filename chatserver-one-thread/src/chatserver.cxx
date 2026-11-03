// Chattserver, steg 2a: asynkron med callbacks, alla klienter i en enda tråd.
//
//   Server:  accept-loop, skapar en Session per klient
//   Session: async_read_until en rad -> room.broadcast()
//            deliver() lägger meddelandet i sessionens skrivkö, och
//            async_write skriver ett meddelande i taget från kön
//   Room:    mängden av sessioner, broadcast till alla utom avsändaren
//
// Allt körs i den tråd som anropar ioc.run(), så ingen mutex behövs.
//
// Skrivkön per session löser head-of-line blocking från del 1: en klient som
// slutar läsa fyller bara sin egen kö, och alla andra fortsätter som vanligt.
// Kön är begränsad. När den är full kastas det äldsta meddelandet som ännu
// inte börjat skrivas. Det som async_write just nu skriver ligger utanför kön
// (in_flight) och rörs aldrig, eftersom operationen refererar dess buffert.
//
// Testa med flera: nc localhost 4711, eller client-simulator/chat-sim.py
#include <boost/asio.hpp>
#include <cstdio>
#include <deque>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// Tak för skrivkön, i meddelanden per session. Kön måste rymma en skur: om N
// klienter skickar en rad i samma ögonblick körs N läs-handlers innan någon
// skriv-handler hinner köras, och varje kö växer med N. Med 200 klienter i
// simulatorn och ett tak på 100 kastades meddelanden hos alla, med 1000 inga.
constexpr auto max_queue = std::size_t{1000};

auto to_string(tcp::endpoint const& ep) -> std::string {
    return std::format("{}:{}", ep.address().to_string(), ep.port());
}

// Tar bort radslut ("\n" eller "\r\n").
auto strip_eol(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

class Session;
using SessionPtr = std::shared_ptr<Session>;

// Mängden av anslutna sessioner. Ingen mutex: alla anrop sker från handlers
// som körs i en och samma tråd.
class Room {
public:
    void join(SessionPtr session) { sessions.insert(std::move(session)); }
    void leave(SessionPtr const& session) { sessions.erase(session); }
    void broadcast(std::string const& msg, SessionPtr const& from);  // definieras efter Session
    auto size() const -> std::size_t { return sessions.size(); }

private:
    std::set<SessionPtr> sessions;
};

// En ansluten klient. Äger sig själv via shared_from_this() så länge en
// operation är utestående, precis som i echo-servern.
class Session : public std::enable_shared_from_this<Session> {
    using super = std::enable_shared_from_this<Session>;

public:
    Session(tcp::socket sock, Room& room)
        : socket{std::move(sock)}, room{room}, name{to_string(socket.remote_endpoint())} {}

    void start() {
        auto self = super::shared_from_this();
        room.join(self);
        std::println("ansluten: {} ({} totalt)", name, room.size());
        room.broadcast(std::format("*** {} anslöt\n", name), self);
        do_read();
    }

    // Anropas av Room, inifrån någon annan sessions läs-handler. Lägger
    // meddelandet i kön och startar en skrivning om ingen pågår. Blockerar aldrig.
    void deliver(std::string msg) {
        if (queue.size() >= max_queue) {
            queue.pop_front();  // äldsta ej påbörjade. in_flight rörs inte.
            ++dropped;
        }
        queue.push_back(std::move(msg));
        if (!in_flight) do_write();
    }

private:
    void do_read() {
        auto self = super::shared_from_this();
        asio::async_read_until(socket, asio::dynamic_buffer(read_buf), '\n',
            [self](boost::system::error_code ec, std::size_t n) {
                if (ec) {
                    self->close();  // eof, eller operation_aborted efter ett skrivfel
                    return;
                }
                auto line = strip_eol(std::string_view{self->read_buf}.substr(0, n));
                self->room.broadcast(std::format("[{}] {}\n", self->name, line), self);
                self->read_buf.erase(0, n);
                self->do_read();
            });
    }

    // Tar nästa meddelande ur kön och skriver det. Handlern tar nästa, tills
    // kön är tom. async_write får aldrig startas igen innan föregående är klar,
    // och in_flight är det som garanterar det.
    void do_write() {
        in_flight = std::move(queue.front());
        queue.pop_front();

        auto self = super::shared_from_this();
        asio::async_write(socket, asio::buffer(*in_flight),
            [self](boost::system::error_code ec, std::size_t) {
                if (ec) {
                    self->close();
                    return;
                }
                self->in_flight.reset();
                if (!self->queue.empty()) self->do_write();
            });
    }

    // Idempotent: anropas från både läs- och skriv-handlern. Att stänga socketen
    // avbryter den andra utestående operationen med operation_aborted, och dess
    // handler hamnar här igen, men då är socketen redan stängd.
    void close() {
        if (!socket.is_open()) return;
        auto ec = boost::system::error_code{};
        socket.close(ec);

        auto self = super::shared_from_this();
        room.leave(self);
        std::println("nedkopplad: {} ({} kvar{})", name, room.size(),
                     dropped ? std::format(", kastade {} meddelanden", dropped) : "");
        room.broadcast(std::format("*** {} kopplade ned\n", name), self);
    }

    tcp::socket socket;
    Room& room;
    std::string name;
    std::string read_buf;
    std::deque<std::string> queue;        // väntar på att skrivas
    std::optional<std::string> in_flight; // skrivs just nu, får inte röras
    std::size_t dropped = 0;
};

void Room::broadcast(std::string const& msg, SessionPtr const& from) {
    for (auto const& session : sessions) {
        if (session != from) session->deliver(msg);
    }
}

// Accept-loopen. Här räcker [this], eftersom Server är en lokal variabel i
// main och lever tills run() returnerar.
class Server {
public:
    Server(asio::io_context& ioc, unsigned short port)
        : acceptor{ioc, tcp::endpoint{tcp::v4(), port}} {
        do_accept();
    }

private:
    void do_accept() {
        acceptor.async_accept([this](boost::system::error_code ec, tcp::socket sock) {
            if (ec) {
                std::println("accept: {}", ec.message());
                return;
            }
            std::make_shared<Session>(std::move(sock), room)->start();
            do_accept();
        });
    }

    tcp::acceptor acceptor;
    Room room;
};

int main(int argc, char** argv) try {
    auto port = static_cast<unsigned short>(argc > 1 ? std::stoi(argv[1]) : 4711);

    auto ioc = asio::io_context{};
    auto server = Server{ioc, port};
    std::println("chattserver (en tråd) lyssnar på port {}", port);

    ioc.run();
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
