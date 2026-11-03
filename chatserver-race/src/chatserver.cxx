// Chattserver, steg 2b (första försöket): steg 2a oförändrad, men run() körs i
// N trådar. Handlers för olika sessioner körs nu samtidigt, och Room samt
// skrivköerna delas mellan trådar utan skydd. Det är en data race.
//
// Bygg med tsan-presetet och kör simulatorn, så pekar ThreadSanitizer ut
// exakt vilka rader som krockar:
//
//   cmake --preset tsan && cmake --build --preset tsan
//   ./build/tsan/chatserver-race/chatserver-race 4711 4
//   client-simulator/chat-sim.py --clients 50 --messages 5
//
// Lösningen, strand:ar, finns i chatserver-strands.
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
#include <thread>
#include <utility>
#include <vector>

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
    std::set<SessionPtr> sessions;

public:
    void join(SessionPtr session) { sessions.insert(std::move(session)); }
    void leave(SessionPtr const& session) { sessions.erase(session); }
    void broadcast(std::string const& msg, SessionPtr const& from);  // definieras efter Session
    auto size() const -> std::size_t { return sessions.size(); }
};

// En ansluten klient. Äger sig själv via shared_from_this() så länge en
// operation är utestående, precis som i echo-servern.
class Session : public std::enable_shared_from_this<Session> {
    using super = std::enable_shared_from_this<Session>;

    tcp::socket socket;
    Room& room;
    std::string name;
    std::string read_buf;
    std::deque<std::string> queue;        // väntar på att skrivas
    std::optional<std::string> in_flight; // skrivs just nu, får inte röras
    std::size_t dropped = 0;

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
        if (is_full()) {
            queue.pop_front();  // äldsta ej påbörjade. in_flight rörs inte.
            ++dropped;
        }
        queue.push_back(std::move(msg));
        if (!in_flight) do_write();
    }

private:
    bool is_full() const { return queue.size() >= max_queue; }

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
};

void Room::broadcast(std::string const& msg, SessionPtr const& from) {
    for (auto const& session : sessions) {
        if (session != from) session->deliver(msg);
    }
}

// Accept-loopen. Här räcker [this], eftersom Server är en lokal variabel i
// main och lever tills run() returnerar.
class Server {
    tcp::acceptor acceptor;
    Room room;

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
};

int main(int argc, char** argv) try {
    auto port = static_cast<unsigned short>(argc > 1 ? std::stoi(argv[1]) : 4711);
    auto num_threads = argc > 2 ? std::stoul(argv[2]) : std::thread::hardware_concurrency();

    auto ioc = asio::io_context{};
    auto server = Server{ioc, port};
    std::println("chattserver (N trådar, utan strand:ar) lyssnar på port {} med {} trådar", port, num_threads);

    // Samma io_context, run() i flera trådar. jthread joinar i destruktorn.
    auto threads = std::vector<std::jthread>{};
    for (auto i = 0u; i < num_threads; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
