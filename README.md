# asio-chatt-del-2

Demoprogram till artikeln **[Bygg en chattserver med Boost.Asio, del 2: Asynkront med callbacks](https://www.ribomation.se/blog/2026/chattserver-boost-asio-del-2/)**
på [ribomation.se](https://www.ribomation.se/blog/).

Artikeln är den andra av fem om Boost.Asio. I [del 1](https://github.com/ribomation/asio-chatt-del-1) kostade
chattservern en tråd per klient, och en enda klient som slutade läsa stoppade broadcast för alla. Den här delen
skriver om samma program **asynkront med callbacks**: en tråd räcker, ingen mutex behövs, och en skrivkö per
session gör att en långsam klient bara sinkar sig själv. Sedan kör vi `run()` i flera trådar, ser racen med
ThreadSanitizer, och löser den med strand:ar.

| Del | Innehåll | Repo |
|---|---|---|
| 1 | Grunderna och synkron I/O | [`asio-chatt-del-1`](https://github.com/ribomation/asio-chatt-del-1) |
| 2 | Asynkront med callbacks | detta repo |
| 3 | Asynkront med coroutines | `asio-chatt-del-3` |
| 4 | TLS, UDP och graceful shutdown | `asio-chatt-del-4` |
| 5 | Admin-UI med Boost.Beast och WebSocket | `asio-chatt-del-5` |

## Programmen

| Mapp | Program | Visar |
|---|---|---|
| [timer/](timer/) | `timer` | Minsta möjliga asynkrona program: `steady_timer::async_wait()` och `io_context::run()`. Anropet returnerar direkt, handlern körs två sekunder senare i samma tråd som `run()`. |
| [echoserver/](echoserver/) | `echoserver` | Echo-servern från del 1 med callbacks. `Session` med `enable_shared_from_this`, kedjan `async_read_some` → `async_write` → `async_read_some`. Flera klienter samtidigt, en tråd. |
| [daytime-client/](daytime-client/) | `daytime-client` | Daytime-klienten från del 1 som tre nästlade lambdas: `async_resolve` → `async_connect` → `async_read`. Callback-helvetet i sin mildaste form, och det som del 3 skriver om med `co_await`. |
| [chatserver-one-thread/](chatserver-one-thread/) | `chatserver-one-thread` | Chatt steg 2a: `Server`/`Session`/`Room` i en tråd. Skrivkö per session med tak, som löser head-of-line blocking från del 1. |
| [chatserver-race/](chatserver-race/) | `chatserver-race` | Steg 2a oförändrad, men `run()` i N trådar. Finns bara för att visa racen med ThreadSanitizer. Kraschar under last. |
| [chatserver-strands/](chatserver-strands/) | `chatserver-strands` | Chatt steg 2b: samma server med en strand per session och en för rummet. N trådar, ingen mutex, inga varningar från ThreadSanitizer. |
| [client-simulator/](client-simulator/) | `chat-sim.py` | Quick & dirty simulator i Python (oförändrad från del 1) som startar N klienter mot chattservern. |

Felhantering: i asynkron kod finns ingen att kasta till, så alla handlers får en `error_code`. Exceptions används
bara i `main`, för fel vid uppstart som "porten är upptagen".

## Förutsättningar

- Boost 1.92 (Asio är header-only, men länkar mot pthreads)
- GCC 16 och C++26 (`std::println`, `std::format`, `std::jthread`)
- CMake 3.28 eller senare, Ninja
- `nc` (netcat) som klient, Python 3 för simulatorn
- För race-demonstrationen: ThreadSanitizer, som följer med GCC

CMake-konfigurationen i [cmake/common.cmake](cmake/common.cmake) hittar Boost via handskrivna
find-moduler i `~/Libs/CMake-modules` (se [Kom igång med Boost](https://www.ribomation.se/blog/2026/kom-igang-med-boost/)).
Har du Boost någon annanstans räcker det att byta ut raden `list(APPEND CMAKE_MODULE_PATH ...)`
mot `-DBoost_ROOT=/sökväg/till/boost`, eller låta `find_package(Boost CONFIG)` hitta en systeminstallation.

## Bygga

Alla program från projektroten, med presets (`debug`, `release` eller `tsan`):

```sh
cmake --preset release
cmake --build --preset release
```

Binärerna hamnar i `build/release/<mapp>/`. Ett program i taget, från dess egen mapp:

```sh
cd echoserver
cmake -S . -B build -G Ninja
cmake --build build
./build/echoserver
```

## Köra

Alla servrar tar porten som valfritt första argument. De två flertrådade tar antal trådar som andra argument,
med `std::thread::hardware_concurrency()` som default.

| Program | Argument | Default | Klient |
|---|---|---|---|
| `timer` | | | |
| `echoserver` | `[port]` | 7000 | `nc localhost 7000`, gärna flera samtidigt |
| `daytime-client` | `[värd] [port]` | localhost 1313 | servern är `daytime-server` från del 1, eller `(date \| nc -l -q 0 1313 &)` |
| `chatserver-one-thread` | `[port]` | 4711 | `nc localhost 4711` i flera terminaler, eller simulatorn |
| `chatserver-race` | `[port] [trådar]` | 4711, alla kärnor | simulatorn, se nedan |
| `chatserver-strands` | `[port] [trådar]` | 4711, alla kärnor | `nc localhost 4711` i flera terminaler, eller simulatorn |

Simulatorn, 200 klienter som skickar 5 rader var med slumpmässiga mellanrum:

```sh
client-simulator/chat-sim.py --clients 200 --messages 5
```

### En tråd räcker

Trådar i `chatserver-one-thread` medan simulatorn kör med 200 klienter. I del 1 var det 202.

```sh
ps -T -p $(pgrep -x chatserver-one-) | tail -n +2 | wc -l
```

```text
1
```

### Head-of-line blocking, löst

Samma experiment som i del 1: anslut en `nc` och pausa den med Ctrl-Z, anslut en till, och låt en tredje klient
skicka 40 000 rader:

```sh
python3 -c 'import socket; s=socket.create_connection(("localhost",4711)); [s.sendall(b"x"*200+b"\n") for _ in range(40000)]'
```

I del 1 stannade allt. Nu får den vakna `nc`:n alla rader, och den pausade fyller bara sin egen kö. När kön är
full kastas det äldsta meddelandet som ännu inte börjat skrivas. Ta tillbaka den pausade med `fg` och avsluta
den med Ctrl-C, så loggar servern hur många som kastades:

```text
nedkopplad: 127.0.0.1:44734 (1 kvar, kastade 24208 meddelanden)
```

### Racen, och strand:arna som löser den

Bygg med `tsan`-presetet och kör `chatserver-race` med fyra trådar under simulatorn:

```sh
cmake --preset tsan && cmake --build --preset tsan
./build/tsan/chatserver-race/chatserver-race 4711 4
client-simulator/chat-sim.py --clients 50 --messages 5
```

ThreadSanitizer rapporterar en data race per krock, med de två trådarnas stackar. Nästan alla pekar på
skrivkön i `Session::deliver()` och `Session::do_write()`, en på `Room::broadcast()`. Efter ett tag kraschar
servern med SEGV i `deliver()`, för en `std::deque` som två trådar skriver till samtidigt är inte en `deque` länge.

```text
WARNING: ThreadSanitizer: data race (pid=753483)
  Read of size 8 at 0x724400014230 by thread T3:
    #2 Session::is_full() const chatserver.cxx:99
    #3 Session::deliver(std::string) chatserver.cxx:90
  Previous write of size 8 at 0x724400014230 by thread T1:
    #2 std::deque<std::string>::push_back(std::string&&)
    #3 Session::deliver(std::string) chatserver.cxx:94
SUMMARY: ThreadSanitizer: data race chatserver.cxx:99 in Session::is_full() const
```

Samma körning med `chatserver-strands` ger inga varningar:

```sh
./build/tsan/chatserver-strands/chatserver-strands 4711 4
client-simulator/chat-sim.py --clients 50 --messages 5
```

## Fördjupning

Detaljer som artikeln bara snuddar vid:

- **Skrivkön måste rymma en skur.** Med tak 100 och 200 simulatorklienter kastades meddelanden hos *alla*
  sessioner, trots att klienterna läste flitigt. Orsaken: när N klienter skickar en rad i samma ögonblick körs N
  läs-handlers innan någon skriv-handler hinner köras, och varje kö växer med N. Taket är därför 1000. Verifierat
  med 200 klienter som alla skickar efter exakt 2 sekunder (`--min-delay 2 --max-delay 2`).
- **Meddelandet under skrivning ligger utanför kön.** `async_write` refererar sin buffert tills handlern körs.
  Hade det legat kvar i `std::deque` hade `pop_front()` vid fullt tak kunnat frigöra minne som en pågående
  skrivning läser från. Därför `std::optional<std::string> in_flight`.
- **`close()` är idempotent.** Både läs- och skriv-handlern kan hamna där. Den första stänger socketen, vilket
  avbryter den andra operationen med `operation_aborted`, och dess handler anropar `close()` igen. Då är
  `socket.is_open()` redan falskt.
- **`[this]` i `Server`, `self` i `Session`.** `Server` är en lokal variabel i `main` och lever tills `run()`
  returnerar, så en rå pekare räcker. `Session` ägs inte av någon annan än sina egna utestående operationer,
  och måste därför fånga en `shared_ptr` till sig själv.
- **`using super = std::enable_shared_from_this<Session>;`** och anropet `super::shared_from_this()` är bara
  för läsbarheten: det syns varifrån metoden kommer.
- **Strand via `async_accept`.** `acceptor.async_accept(asio::make_strand(ioc), handler)` ger en socket vars
  executor är en ny strand, så alla dess handlers hamnar där utan `bind_executor`. Handlern kan fortfarande ta
  emot den som `tcp::socket`, eftersom `any_io_executor` kan hålla en strand.
- **`post` kontra `dispatch`.** `deliver()` använder `asio::post`, som alltid köar. `asio::dispatch` hade kört
  funktionen direkt om anroparen redan stod på rätt strand. Här står vi alltid på rummets strand när vi
  anropar `deliver()`, så det hade inte gjort någon skillnad, och `post` är lättare att resonera om.
- **Loggningen av antal i `Room`.** I steg 2a hade `Session` en `room.size()` i sin loggrad. Med strand:ar vore
  det en läsning av `sessions` från sessionens strand, alltså en race. Därför loggar `Room::join()` och
  `Room::leave()` själva, på rummets strand.

## Relaterade artiklar

- [Bygg en chattserver med Boost.Asio, del 1](https://www.ribomation.se/blog/2026/chattserver-boost-asio-del-1/)
- [Kom igång med Boost](https://www.ribomation.se/blog/2026/kom-igang-med-boost/)
- [Väderdata med Boost](https://www.ribomation.se/blog/2026/vaderdata-med-boost/)
- [Asynkrona operationer med coroutines](https://www.ribomation.se/blog/2026/asynkrona-operationer-med-coroutines/)
