// Chattserver, steg 2b: callbacks med run() i N trådar, synkroniserat med strand:ar.
//
// En strand är en executor som garanterar att handlers som går genom den
// aldrig körs samtidigt, i den ordning de skickades. Ingen tråd blockerar:
// en mutex får tråden att vänta, en strand får arbetet att vänta.
//
// Varje bit delad data har exakt en strand, och all kod som rör datan går
// genom den:
//
//   Session: socketen skapas på en egen strand (async_accept med executor),
//            så alla dess handlers körs där. Skrivkön rörs bara på den strand:en.
//            deliver() anropas från rummets strand och postar till sessionens.
//   Room:    har en egen strand. join/leave/broadcast postar dit.
//
// Testa med flera: nc localhost 4711, eller client-simulator/chat-sim.py
// Bygg med tsan-presetet och jämför med chatserver-race: inga varningar.
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

// Tak för skrivkön, i meddelanden per session. Se chatserver-one-thread.
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

// Mängden av anslutna sessioner. Alla publika metoder postar till rummets
// strand, så att sessions bara rörs från en handler i taget. Loggningen av
// antalet ligger här, eftersom size() annars hade lästs från fel strand.
class Room {
    asio::strand<asio::io_context::executor_type> strand;
    std::set<SessionPtr> sessions;

public:
    explicit Room(asio::io_context& ioc) : strand{asio::make_strand(ioc)} {}

    void join(SessionPtr session);
    void leave(SessionPtr session, std::size_t dropped);
    void broadcast(std::string msg, SessionPtr from);

private:
    void do_broadcast(std::string const& msg, SessionPtr const& from);  // körs på strand
};

// En ansluten klient. Socketen sitter på sin egen strand, och alla handlers
// nedan körs där. Det enda som anropas utifrån är deliver(), och den postar.
class Session : public std::enable_shared_from_this<Session> {
    using super = std::enable_shared_from_this<Session>;

    tcp::socket socket;  // dess executor är sessionens strand
    Room& room;
    std::string name_;
    std::string read_buf;
    std::deque<std::string> queue;        // väntar på att skrivas
    std::optional<std::string> in_flight; // skrivs just nu, får inte röras
    std::size_t dropped = 0;

public:
    Session(tcp::socket sock, Room& room)
        : socket{std::move(sock)}, room{room}, name_{to_string(socket.remote_endpoint())} {}

    auto name() const -> std::string const& { return name_; }

    void start() {
        room.join(super::shared_from_this());
        do_read();
    }

    // Anropas från rummets strand. Får inte röra kön direkt, utan postar till
    // sessionens strand. post() köar alltid; dispatch() hade kört direkt om vi
    // redan stod på rätt strand, men det gör vi aldrig här.
    void deliver(std::string msg) {
        auto self = super::shared_from_this();
        asio::post(socket.get_executor(), [self, msg = std::move(msg)]() mutable {
            self->enqueue(std::move(msg));
        });
    }

private:
    bool is_full() const { return queue.size() >= max_queue; }

    // Körs på sessionens strand. Samma kö som i steg 2a.
    void enqueue(std::string msg) {
        if (!socket.is_open()) return;  // deliver() hann före leave()
        if (is_full()) {
            queue.pop_front();  // äldsta ej påbörjade. in_flight rörs inte.
            ++dropped;
        }
        queue.push_back(std::move(msg));
        if (!in_flight) do_write();
    }

    void do_read() {
        auto self = super::shared_from_this();
        asio::async_read_until(socket, asio::dynamic_buffer(read_buf), '\n',
            [self](boost::system::error_code ec, std::size_t n) {
                if (ec) {
                    self->close();
                    return;
                }
                auto line = strip_eol(std::string_view{self->read_buf}.substr(0, n));
                self->room.broadcast(std::format("[{}] {}\n", self->name_, line), self);
                self->read_buf.erase(0, n);
                self->do_read();
            });
    }

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

    void close() {
        if (!socket.is_open()) return;
        auto ec = boost::system::error_code{};
        socket.close(ec);
        room.leave(super::shared_from_this(), dropped);
    }
};

void Room::join(SessionPtr session) {
    asio::post(strand, [this, session = std::move(session)] {
        sessions.insert(session);
        std::println("ansluten: {} ({} totalt)", session->name(), sessions.size());
        do_broadcast(std::format("*** {} anslöt\n", session->name()), session);
    });
}

void Room::leave(SessionPtr session, std::size_t dropped) {
    asio::post(strand, [this, session = std::move(session), dropped] {
        sessions.erase(session);
        std::println("nedkopplad: {} ({} kvar{})", session->name(), sessions.size(),
                     dropped ? std::format(", kastade {} meddelanden", dropped) : "");
        do_broadcast(std::format("*** {} kopplade ned\n", session->name()), session);
    });
}

void Room::broadcast(std::string msg, SessionPtr from) {
    asio::post(strand, [this, msg = std::move(msg), from = std::move(from)] {
        do_broadcast(msg, from);
    });
}

void Room::do_broadcast(std::string const& msg, SessionPtr const& from) {
    for (auto const& session : sessions) {
        if (session != from) session->deliver(msg);
    }
}

// Accept-loopen. Varje ny socket skapas på en egen strand, genom att en
// executor ges som första argument till async_accept.
class Server {
    asio::io_context& ioc;
    tcp::acceptor acceptor;
    Room room;

public:
    Server(asio::io_context& ioc, unsigned short port)
        : ioc{ioc}, acceptor{ioc, tcp::endpoint{tcp::v4(), port}}, room{ioc} {
        do_accept();
    }

private:
    void do_accept() {
        acceptor.async_accept(asio::make_strand(ioc), [this](boost::system::error_code ec, tcp::socket sock) {
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
    std::println("chattserver (strand:ar) lyssnar på port {} med {} trådar", port, num_threads);

    // Samma io_context, run() i flera trådar. jthread joinar i destruktorn.
    auto threads = std::vector<std::jthread>{};
    for (auto i = 0u; i < num_threads; ++i) {
        threads.emplace_back([&ioc] { ioc.run(); });
    }
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
