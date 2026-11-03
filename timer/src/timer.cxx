// Minsta möjliga asynkrona program: en timer som går av efter två sekunder.
//
//   async_wait() startar operationen och returnerar direkt.
//   Handlern körs senare, inne i ioc.run(), i den tråd som anropade run().
//   run() returnerar när det inte finns mer arbete att vänta på.
#include <boost/asio.hpp>
#include <chrono>
#include <print>
#include <thread>

namespace asio = boost::asio;
using namespace std::chrono_literals;
using clk = std::chrono::steady_clock;

auto ms_since(clk::time_point start) -> long {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - start).count();
}

int main() {
    auto start = clk::now();
    auto ioc = asio::io_context{};
    auto timer = asio::steady_timer{ioc, 2s};

    timer.async_wait([start](boost::system::error_code ec) {
        std::println("[{:5} ms] handlern körs i tråd {}: {}",
                     ms_since(start), std::this_thread::get_id(), ec ? ec.message() : "timern gick av");
    });
    std::println("[{:5} ms] async_wait returnerade direkt, huvudtråden är {}",
                 ms_since(start), std::this_thread::get_id());

    ioc.run();
    std::println("[{:5} ms] run() returnerade: inget mer arbete", ms_since(start));
}
