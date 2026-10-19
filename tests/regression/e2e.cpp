#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <memory>
#include <poll.h>
#include <pty.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "Checks.h"
#include "Terminal.h"

using namespace regression;
using Clock = std::chrono::steady_clock;
constexpr const char *UP = "\033OA";
constexpr const char *DOWN = "\033OB";
volatile std::sig_atomic_t interrupted = 0;
void onSignal(int signal)
{
    interrupted = signal;
}
struct Options
{
    fs::path binary = "build/AnoDrop", output = "tests/regression/artifacts";
    fs::path configA, configB;
    int bootstrap = 600, handshake = 600, message = 600, ui = 60, shutdown = 180;
    bool extended = false;
    bool smoke = false;
};
struct Runner;
struct Peer
{
    Runner &run;
    std::string name, password = "regression-password", location = "menu";
    fs::path root;
    StoreReader reader;
    Terminal screen;
    pid_t pid = -1;
    int fd = -1, menuIndex = 1, generation = 0, exitStatus = 0;
    std::ofstream raw;
    Peer(Runner &owner, std::string label, fs::path path) : run(owner), name(std::move(label)), root(std::move(path)), reader(root / "store")
    {
        fs::create_directories(root);
    }
    ~Peer()
    {
        forceClose();
    }
    void forceClose()
    {
        if (pid > 0)
        {
            kill(pid, SIGKILL);
            while (waitpid(pid, &exitStatus, 0) < 0 && errno == EINTR)
            {
            }
            pid = -1;
        }
        if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }
        raw.close();
    }
    void pump()
    {
        if (fd < 0)
            return;
        char buffer[32768];
        for (int i = 0; i < 32; ++i)
        {
            auto n = ::read(fd, buffer, sizeof(buffer));
            if (n <= 0)
                break;
            std::string data(buffer, static_cast<size_t>(n));
            raw << data;
            raw.flush();
            screen.feed(data);
        }
    }
    bool alive()
    {
        if (pid <= 0)
            return false;
        auto result = waitpid(pid, &exitStatus, WNOHANG);
        if (result == pid)
        {
            pid = -1;
            return false;
        }
        return result == 0;
    }
    void send(const std::string &data)
    {
        require(alive(), name + ": process exited");
        size_t offset = 0;
        auto deadline = Clock::now() + std::chrono::seconds(60);
        while (offset < data.size())
        {
            auto n = ::write(fd, data.data() + offset, data.size() - offset);
            if (n > 0)
                offset += static_cast<size_t>(n);
            else if (errno != EAGAIN && errno != EINTR)
                throw std::runtime_error("PTY write failed");
            else
            {
                require(Clock::now() < deadline, "PTY write timeout");
                pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    LobbyData data()
    {
        auto all = reader.read();
        require(all.size() == 1, name + ": expected exactly one lobby on disk");
        return all[0].get<LobbyData>();
    }
    void expect(const std::string &text, int seconds = 0);
    void sendMessage(const std::string& token);
    void start(bool wrong = false);
    void menu();
    void choose(int option);
    void lobbies();
    void chat();
    void handshake(bool duplicate = false, bool restore = false);
    void stop(bool crash = false);
};
struct Runner
{
    Options options;
    fs::path root;
    std::string channel;
    std::vector<std::unique_ptr<Peer>> peers;
    json results = json::array();
    json observations = json::array();
    int stepNumber = 0;
    explicit Runner(Options opts) : options(std::move(opts))
    {
        options.binary = fs::absolute(options.binary);
        require(fs::is_regular_file(options.binary), "Missing binary " + options.binary.string());
        fs::create_directories(fs::absolute(options.output));
        auto pattern = (fs::absolute(options.output) / "run-XXXXXX").string();
        require(mkdtemp(pattern.data()) != nullptr, "Cannot create artifact directory");
        root = pattern;
        channel = "regression-" + root.filename().string() + "-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        if (options.configA.empty() && fs::exists("configtest1.json"))
            options.configA = fs::absolute("configtest1.json");
        if (options.configB.empty() && fs::exists("configtest2.json"))
            options.configB = fs::absolute("configtest2.json");

        peers.push_back(std::make_unique<Peer>(*this, "Alice", root / "Alice"));
        peers.push_back(std::make_unique<Peer>(*this, "Bob", root / "Bob"));
        std::cout << "Artifacts: " << root << std::endl;
        if (!options.configA.empty())
            std::cout << "  Alice base config: " << options.configA << std::endl;
        if (!options.configB.empty())
            std::cout << "  Bob base config:   " << options.configB << std::endl;
    }
    void wait(const std::string &label, const std::function<bool()> &predicate, int seconds, bool allowExit = false)
    {
        auto deadline = Clock::now() + std::chrono::seconds(seconds), progress = Clock::now() + std::chrono::seconds(30);
        std::string last = "condition false";
        while (true)
        {
            require(!interrupted, "Run interrupted by signal " + std::to_string(interrupted));
            for (auto &p : peers)
            {
                p->pump();
                if (p->fd >= 0 && !allowExit)
                    require(p->alive(), p->name + ": process died while waiting for " + label);
            }
            try
            {
                if (predicate())
                    return;
            }
            catch (const std::exception &e)
            {
                last = e.what();
            }
            auto now = Clock::now();
            if (now >= deadline)
                throw WaitTimeout("Timeout (" + std::to_string(seconds) + "s): " + label + "; " + last);
            if (now >= progress)
            {
                std::cout << "  waiting: " << label << " (" << std::chrono::duration_cast<std::chrono::seconds>(deadline - now).count() << "s remaining)" << std::endl;
                progress = now + std::chrono::seconds(30);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    void report()
    {
        writeText(root / "report.json", json{{"binary", options.binary.string()}, {"channel", channel}, {"base_configs", {options.configA.string(), options.configB.string()}}, {"mode", options.smoke ? "messaging-smoke" : "full"}, {"timeouts_seconds", {{"bootstrap", options.bootstrap}, {"handshake", options.handshake}, {"message", options.message}, {"ui", options.ui}, {"shutdown", options.shutdown}}}, {"results", results}, {"delivery_observations", observations}}.dump(2));
        std::string markdown = "# AnoDrop regression\n\n";
        for (const auto &r : results)
            markdown += "- " + r.at("status").get<std::string>() + ": " + r.at("name").get<std::string>() + (r.contains("error") ? " — " + r["error"].get<std::string>() : "") + "\n";
        writeText(root / "report.md", markdown);
    }
    void snapshot()
    {
        for (auto &p : peers)
        {
            p->pump();
            auto prefix = std::to_string(stepNumber) + "-";
            writeText(p->root / (prefix + "screen.txt"), p->screen.text());
            try
            {
                writeText(p->root / (prefix + "store.json"), p->reader.read().dump(2));
            }
            catch (const std::exception &e)
            {
                writeText(p->root / (prefix + "snapshot-error.txt"), e.what());
            }
            if (fs::exists(p->root / "app.log"))
                writeText(p->root / "log-analysis.json", analyzeLogs(readText(p->root / "app.log")).dump(2));
        }
    }
    void step(const std::string &name, const std::function<void()> &action)
    {
        ++stepNumber;
        std::cout << "[" << stepNumber << "] " << name << std::endl;
        auto start = Clock::now();
        json result = {{"name", name}, {"status", "passed"}};
        try
        {
            action();
        }
        catch (const std::exception &e)
        {
            result["status"] = "failed";
            result["failure_kind"] = dynamic_cast<const WaitTimeout *>(&e) ? "timeout_unclassified" : "check_failed";
            result["regression_confirmed"] = false;
            result["error"] = e.what();
            result["seconds"] = std::chrono::duration<double>(Clock::now() - start).count();
            results.push_back(result);
            snapshot();
            report();
            throw;
        }
        result["seconds"] = std::chrono::duration<double>(Clock::now() - start).count();
        results.push_back(result);
        snapshot();
        report();
    }
    void consistent(Peer &peer, const std::string &token = "", MessageDirection direction = MessageDirection::OUT)
    {
        wait(peer.name + ": persisted JSON / screen agree", [&]
             {
            auto d=peer.data(); validate(d);
            if(!token.empty()) checkScreen(d,peer.screen.text(),token,direction);
            return true; }, options.message);
    }
    void delivered(Peer &sender, Peer &receiver, const std::string &token)
    {
        int64_t seq = 0;
        auto observe = [&]
        {
            auto state = deliveryObservation(sender.data(), receiver.data(), token);
            state["sender"] = sender.name;
            state["receiver"] = receiver.name;
            state["sender_screen"] = sender.screen.text();
            state["receiver_screen"] = receiver.screen.text();
            observations.push_back(std::move(state));
        };
        std::cout << "    [Delivery check: '" << token << "']" << std::endl;
        try
        {
            std::cout << "      -> Step 1/4: Waiting for message reception by " << receiver.name << "..." << std::endl;
            wait(token + " (Step 1/4): persisted reception", [&]
                 {
                auto source = sender.data(), target = receiver.data();
                auto out = findMessage(source, token, MessageDirection::OUT);
                auto in = findMessage(target, token, MessageDirection::IN);
                seq = out.seq_num;
                require(seq == in.seq_num && out.fake_timestamp == in.fake_timestamp, "Payload sequence/timestamp mismatch");
                return true; }, options.message);
            std::cout << "         ✓ Received (Seq: " << seq << ")" << std::endl;
            observe();

            std::cout << "      -> Step 2/4: Waiting for DHT confirmation (dht_ack for seq " << seq << ")..." << std::endl;
            wait(token + " (Step 2/4): DHT confirmation in JSON", [&]
                 { return sender.data().sync.dht_acked_seq_nums.acknowledges(seq); }, options.message);
            std::cout << "         ✓ DHT confirmed publication" << std::endl;
            observe();

            std::cout << "      -> Step 3/4: Waiting for peer ACK from " << receiver.name << " (peer_ack for seq " << seq << ")..." << std::endl;
            wait(token + " (Step 3/4): peer ACK in JSON", [&]
                 { return sender.data().sync.peer_acked_seq_nums.acknowledges(seq); }, options.message);
            std::cout << "         ✓ Peer ACK confirmed" << std::endl;
            observe();
        }
        catch (const std::exception &e)
        {
            std::cerr << "      ✗ Delivery check failed on token '" << token << "': " << e.what() << std::endl;
            try
            {
                observe();
            }
            catch (const std::exception &obsErr)
            {
                observations.push_back({{"token", token}, {"observation_error", obsErr.what()}});
            }
            throw;
        }

        std::cout << "      -> Step 4/4: Validating screen and log correlations..." << std::endl;
        consistent(sender, token);
        consistent(receiver, token, MessageDirection::IN);
        // The ACK is committed before its log is queued, and the logger flushes
        // asynchronously. Re-read both files until diagnostics catch up, bounded
        // by the UI timeout. Never repeat the already successful network send.
        wait("Delivery logs flushed for " + token, [&] {
            const auto senderLog = readText(sender.root / "app.log");
            const auto receiverLog = readText(receiver.root / "app.log");
            checkDeliveryLogs(senderLog, receiverLog, channel, seq);
            for (const auto &entry : {std::pair{senderLog, sender.data()}, std::pair{receiverLog, receiver.data()}}) {
                require(entry.first.find('\x1b') == std::string::npos, "ANSI escape in log file");
                for (const auto &secret : {token, std::string(entry.second.crypto->my_x25519_priv),
                                          std::string(entry.second.crypto->private_node_discussion)})
                    require(secret.empty() || entry.first.find(secret) == std::string::npos, "Sensitive data in log file");
            }
            return true;
        }, options.ui);
        std::cout << "         ✓ All delivery validations passed for '" << token << "'" << std::endl;
    }
    void execute();
};
// Wait for the asynchronous UI job before typing the next message. A PTY paste
// containing several Enter keys can otherwise hit Enter while the previous job is pending.
void Peer::sendMessage(const std::string& token)
{
    send(token);
    expect("> " + token + "_");
    send("\n");
    run.wait(name + ": message committed and input cleared", [&] {
        (void)findMessage(data(), token, MessageDirection::OUT);
        return screen.lines[Terminal::rows - 2].find("> _") != std::string::npos;
    }, run.options.ui);
}
void Peer::expect(const std::string &text, int seconds)
{
    run.wait(name + ": screen contains " + text, [&]
             { return screen.text().find(text) != std::string::npos; }, seconds ? seconds : run.options.ui);
}
void Peer::start(bool wrong)
{
    require(fd < 0, "Peer already started");
    screen = Terminal();
    ++generation;
    json config = peerConfig(name == "Alice" ? run.options.configA : run.options.configB, root);
    auto configPath = root / "config.json";
    writeText(configPath, config.dump(2));
    raw.open(root / ("terminal-" + std::to_string(generation) + ".ansi"));
    winsize size{};
    size.ws_row = Terminal::rows;
    size.ws_col = Terminal::cols;
    pid = forkpty(&fd, nullptr, nullptr, &size);
    require(pid >= 0, "forkpty failed: " + std::string(strerror(errno)));
    if (pid == 0)
    {
        setenv("TERM", "xterm", 1);
        if (chdir(root.c_str()) != 0)
            _exit(126);
        execl(run.options.binary.c_str(), run.options.binary.c_str(), "-c", configPath.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    expect("Enter your Master Password");
    if (wrong)
    {
        send("definitely-wrong\n");
        expect("Wrong master password or corrupted vault");
        writeText(root / "wrong-password.screen.txt", screen.text());
    }
    send(password + "\n");
    expect("1. Handshake", run.options.bootstrap);
    menuIndex = 1;
    location = "menu";
}
void Peer::menu()
{
    if (location == "chat")
    {
        send("\t\n");
        expect("=== LOBBIES ===");
        location = "lobbies";
    }
    if (location == "lobbies")
    {
        send("q");
        expect("1. Handshake");
        location = "menu";
    }
}
void Peer::choose(int option)
{
    menu();
    std::string keys;
    for (int i = 0; i < std::abs(option - menuIndex); ++i)
        keys += option > menuIndex ? DOWN : UP;
    send(keys + "\n");
    menuIndex = option;
}
void Peer::lobbies()
{
    choose(2);
    location = "lobbies";
    expect("=== LOBBIES ===");
}
void Peer::chat()
{
    if (location != "lobbies")
        lobbies();
    expect("[READY]", run.options.handshake);
    send("\n");
    expect("=== Chat:");
    location = "chat";
}
void Peer::handshake(bool duplicate, bool restore)
{
    choose(1);
    expect("Enter Channel Name");
    send(run.channel + "\n");
    if (restore)
        expect("Lobby was hidden. It is now restored!");
    else
    {
        expect("Enter your Pseudo");
        send(name + "\n");
        expect(duplicate ? "Lobby already exists!" : "Lobby added to store!", run.options.handshake);
    }
    send(" ");
    expect("1. Handshake");
}
void Peer::stop(bool crash)
{
    if (fd < 0)
        return;
    if (crash)
    {
        forceClose();
        return;
    }
    menu();
    send("q");
    run.wait(name + ": graceful shutdown", [&]
             { return !alive(); }, run.options.shutdown, true);
    pump();
    require(WIFEXITED(exitStatus) && WEXITSTATUS(exitStatus) == 0, name + ": abnormal exit");
    close(fd);
    fd = -1;
    raw.close();
}
void Runner::execute()
{
    auto &a = *peers[0];
    auto &b = *peers[1];
    using Case = std::pair<std::string, std::function<void()>>;
    std::vector<Case> cases{
        {"Startup and empty stores", [&]
         {
             a.start();
             b.start();
             for (auto &p : peers)
             {
                 p->choose(2);
                 p->location = "lobbies";
                 p->expect("No lobbies found");
                 p->menu();
             }
         }},
        {"Handshake PENDING to READY, identities and shared secret", [&]
         {
             a.handshake();
             a.lobbies();
             a.expect("[PENDING]");
             wait("Pending JSON persisted", [&]
                  { return a.data().metadata.status == LobbyStatus::PENDING_HANDSHAKE; }, options.ui);
             b.handshake();
             b.lobbies();
             a.expect("[READY]", options.handshake);
             b.expect("[READY]", options.handshake);
             wait("Handshake JSON agreement", [&]
                  {
                auto x=a.data(),y=b.data();
                require(x.metadata.status==LobbyStatus::HANDSHAKE_DONE && y.metadata.status==LobbyStatus::HANDSHAKE_DONE,"Handshake not persisted");
                require(x.metadata.other_pseudo=="Bob" && y.metadata.other_pseudo=="Alice","Wrong peer identity");
                require(!x.crypto->private_node_discussion.empty() && x.crypto->private_node_discussion==y.crypto->private_node_discussion,"Shared secret mismatch");
                require(x.crypto->my_x25519_pub==y.crypto->announced_x25519_pub && y.crypto->my_x25519_pub==x.crypto->announced_x25519_pub,"Public key mismatch");
                return true; }, options.handshake);
             a.chat();
             b.chat();
         }},
        {"Bidirectional delivery and burst sequences", [&]
         {
             a.sendMessage("first-message");
             delivered(a, b, "first-message");
             b.sendMessage("bidirectional-reply");
             delivered(b, a, "bidirectional-reply");
             for (const auto* token : {"burst-1", "burst-2", "burst-3"}) a.sendMessage(token);
             for (const auto *token : {"burst-1", "burst-2", "burst-3"})
                 delivered(a, b, token);
         }},
        {"Unread indicator and persisted read count", [&]
         {
             b.menu();
             b.lobbies();
             a.sendMessage("unread-check");
             b.expect("* NEW MSG *", options.message);
             wait("Unread count persisted", [&]
                  {auto d=b.data();return d.metadata.read_messages_count<d.history.materialize().size(); }, options.message);
             b.chat();
             delivered(a, b, "unread-check");
             wait("Read count persisted", [&]
                  {auto d=b.data();return d.metadata.read_messages_count==d.history.materialize().size(); }, options.ui);
         }},
        {"Empty and oversized messages preserve sequence and history", [&]
         {
             wait("Prior messages leave rate window", [&]
                  {auto d=a.data();auto now=TimeUtils::computeFakeTimestamp(d.metadata.fake_timezone.offset_seconds);for(const auto& m:d.history.materialize()) if(m.direction==MessageDirection::OUT && now-m.fake_timestamp<=10) return false;return true; }, options.ui);
             auto before = a.data();
             a.send("\n");
             a.menu();
             a.chat();
             require(a.data().history == before.history && a.data().sync.my_seq_num == before.sync.my_seq_num, "Empty message consumed a sequence");
             a.send(std::string(1100, 'x') + "\n");
             a.expect("Message exceeds BEP44 size limit.");
             a.send(std::string(1100, '\177'));
             a.menu();
             a.chat();
             require(a.data().history == before.history && a.data().sync.my_seq_num == before.sync.my_seq_num, "Oversized message persisted or consumed a sequence");
         }},
        {"Hide restore and duplicate lobby rejection", [&]
         {
             auto before = a.data().history;
             a.menu();
             a.lobbies();
             a.send("d");
             a.expect("No lobbies found");
             wait("Hidden persisted", [&]
                  { return a.data().metadata.is_hidden; }, options.ui);
             a.handshake(false, true);
             wait("Restored persisted", [&]
                  { return !a.data().metadata.is_hidden; }, options.ui);
             require(a.data().history == before, "Restore lost history");
             a.handshake(true);
             a.chat();
         }},
        {"Graceful restart, wrong password and sequence continuity", [&]
         {
             auto before = a.data();
             a.stop();
             a.start(true);
             a.chat();
             consistent(a);
             require(a.data().history == before.history && a.data().sync.my_seq_num == before.sync.my_seq_num, "Restart changed history/counter");
             a.sendMessage("after-restart");
             delivered(a, b, "after-restart");
         }},
        {"Offline send, resync, crash recovery and outbox", [&]
         {
             b.stop();
             a.sendMessage("offline-message");
             int64_t original = 0;
             wait("Offline message persisted and DHT confirmed", [&]
                  {auto d=a.data();original=findMessage(d,"offline-message",MessageDirection::OUT).seq_num;return d.sync.dht_acked_seq_nums.acknowledges(original); }, options.message);
             require(!a.data().sync.peer_acked_seq_nums.acknowledges(original), "Offline peer falsely ACKed new message");
             consistent(a, "offline-message");
             a.send("\t\t\n");
             wait("Resync persisted", [&]
                  {auto d=a.data();return std::binary_search(d.sync.resynced_seq_nums.begin(),d.sync.resynced_seq_nums.end(),original); }, options.ui);
             std::vector<ChatMessage> copies;
             for (const auto &m : a.data().history.materialize())
                 if (m.direction == MessageDirection::OUT && resyncParent(m) == original)
                     copies.push_back(m);
             require(copies.size() == 1 && copies[0].seq_num == original + 1, "Resync must allocate one new sequence");
             consistent(a, "offline-message");
             consistent(a, copies[0].content);
             a.stop(true);
             a.start();
             a.chat();
             wait("Outbox retries unacknowledged resync copy", [&] {
                 return readText(a.root / "app.log").find("NetworkResyncHandler: Resending message SeqNum " + std::to_string(copies[0].seq_num)) != std::string::npos;
             }, options.message);
             b.start();
             b.chat();
             delivered(a, b, "offline-message");
             delivered(a, b, copies[0].content);
             consistent(a, "offline-message");
             auto before = a.data().sync.my_seq_num;
             a.send("\t\t\n");
             a.menu();
             a.chat();
             require(a.data().sync.my_seq_num == before, "Resync duplicated ACKed messages");
         }},
        {"Empty password rejection and password rotation", [&]
         {
             auto before = a.data().history;
             a.choose(3);
             a.expect("Enter new Master Password");
             a.send("\n");
             a.expect("Password cannot be empty!");
             a.send(" ");
             a.expect("1. Handshake");
             a.choose(3);
             a.expect("Enter new Master Password");
             a.send("regression-changed\n");
             a.expect("Master Password updated successfully!");
             a.password = "regression-changed";
             a.reader.password = a.password;
             a.send(" ");
             a.expect("1. Handshake");
             a.stop();
             a.start(true);
             a.chat();
             require(a.data().history == before, "Password rotation lost history");
         }}};
    if (options.extended)
    {
        cases.emplace_back("Heartbeat connected then disconnected after offline timeout", [&]
                           {
            a.expect("[Connected]",options.message);
            wait("Fresh heartbeat persisted",[&]{return a.data().isPeerConnected();},options.message);
            b.stop();
            a.expect("[Disconnected]",std::max(660,options.message));
            require(!a.data().isPeerConnected(),"UI disconnected but JSON heartbeat still connected");
            b.start(); b.chat(); a.expect("[Connected]",options.message); });
        cases.emplace_back("Rate limiting rejects excess messages without sequence holes", [&]
                           {
            auto before=a.data().sync.my_seq_num;
            // Rate window is 10s; wait for existing sends to leave it before the burst.
            wait("Rate limit window cleared",[&]{auto d=a.data();auto now=TimeUtils::computeFakeTimestamp(d.metadata.fake_timezone.offset_seconds);for(const auto& m:d.history.materialize()) if(m.direction==MessageDirection::OUT && now-m.fake_timestamp<=10) return false;return true;},options.ui);
            for(int i=0;i<10;++i) a.sendMessage("rate-" + std::to_string(i));
            a.send("rate-10\n");
            a.expect("Rate limit reached.");
            wait("Accepted burst persisted",[&]{return a.data().sync.my_seq_num==before+10;},options.ui);
            require(a.data().history.materialize().back().content=="rate-9","Rejected rate message was persisted");
            // Rejected input remains in the edit buffer: clear it before navigating.
            a.send(std::string(7,'\177'));
            for(int i=0;i<10;++i) delivered(a,b,"rate-"+std::to_string(i)); });
    }
    cases.emplace_back("Anomaly status propagation", [&]
                       {
        require(b.location=="chat", "Anomaly test requires Bob's chat to be open");
        b.expect("=== Chat: "+channel);
        a.choose(4); a.expect("Select a lobby to flag"); a.send(std::string(DOWN)+"\n");
        a.expect("Enter Anomaly Message"); a.send("regression-anomaly\n"); a.expect("Anomaly broadcasted");
        a.send(" "); a.expect("1. Handshake"); a.lobbies(); a.expect("[ERROR]");
        // The local "broadcasted" label only means queued. Distinguish remote
        // persistence from rendering instead of reporting every failure as a UI timeout.
        for(auto& p:peers) wait(p->name+": anomaly persisted in JSON",[&]{
            auto d=p->data();
            require(d.metadata.status==LobbyStatus::ANOMALY && d.metadata.anomaly_message=="regression-anomaly",
                "Observed status="+std::to_string(static_cast<int>(d.metadata.status))+", anomaly_message="+d.metadata.anomaly_message);
            return true;
        },options.message);
        b.expect("STATUS: [ANOMALY]",options.ui);
        b.expect("regression-anomaly",options.ui); });
    if (options.smoke)
        cases.resize(3); // Startup, handshake, bidirectional messages and burst.
    cases.emplace_back("Final disk invariants, logs and graceful shutdown", [&]
                       {
        for(auto& p:peers) {
            consistent(*p); p->stop(); validate(p->data());
            auto analysis=analyzeLogs(readText(p->root/"app.log"));
            writeText(p->root/"log-analysis.json",analysis.dump(2));
            require(analysis["errors"].empty(),p->name+": unexpected errors in log-analysis.json");
        } });
    try
    {
        for (const auto &[name, action] : cases)
            step(name, action);
    }
    catch (...)
    {
        for (size_t i = results.size(); i < cases.size(); ++i)
            results.push_back({{"name", cases[i].first}, {"status", "skipped"}, {"reason", "Prerequisite failed"}});
        report();
        throw;
    }
}
int main(int argc, char **argv)
{
    umask(0077);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    try
    {
        Options options;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--help")
            {
                std::cout << "Usage: anodrop_e2e [--binary build/AnoDrop] [--output directory] [--extended]\n"
                          << "  --config-a configtest1.json --config-b configtest2.json (settings only; isolated stores)\n"
                          << "  --smoke (startup, handshake, bidirectional messages and burst only)\n"
                          << "  --bootstrap-timeout 600 --handshake-timeout 600 --message-timeout 600\n"
                          << "  --ui-timeout 60 --shutdown-timeout 180 (all values in seconds)\n";
                return 0;
            }
            if (arg == "--extended")
            {
                options.extended = true;
                continue;
            }
            if (arg == "--smoke")
            {
                options.smoke = true;
                continue;
            }
            require(i + 1 < argc, "Missing value for " + arg);
            std::string value = argv[++i];
            if (arg == "--binary")
                options.binary = value;
            else if (arg == "--output")
                options.output = value;
            else if (arg == "--config-a")
                options.configA = fs::absolute(value);
            else if (arg == "--config-b")
                options.configB = fs::absolute(value);
            else
            {
                size_t parsed = 0;
                int seconds = std::stoi(value, &parsed);
                require(parsed == value.size() && seconds > 0, "Invalid timeout");
                if (arg == "--bootstrap-timeout")
                    options.bootstrap = seconds;
                else if (arg == "--handshake-timeout")
                    options.handshake = seconds;
                else if (arg == "--message-timeout")
                    options.message = seconds;
                else if (arg == "--ui-timeout")
                    options.ui = seconds;
                else if (arg == "--shutdown-timeout")
                    options.shutdown = seconds;
                else
                    throw std::runtime_error("Unknown option " + arg);
            }
        }
        require(!(options.smoke && options.extended), "Choose --smoke or --extended, not both");
        Runner run(options);
        run.execute();
        std::cout << "PASS: " << run.root / "report.json" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
