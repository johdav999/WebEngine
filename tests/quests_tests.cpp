#include "../examples/quests/demo.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
using namespace engine::quests;
namespace {
int checks = 0;
std::ostringstream transitions;
void record(const Transaction &tx) {
    const auto &s = tx.state();
    transitions << s.generation << ':' << s.revision << ':' << s.fingerprint << '\n';
    auto fields = [](const Fields &f) {
        for (const auto &[key, value] : f) {
            transitions << std::quoted(key) << ':' << value.index() << '=';
            std::visit([](const auto &x) { transitions << x; }, value);
            transitions << ';';
        }
        transitions << '\n';
    };
    fields(s.facts);
    for (const auto &[id, p] : s.quests) {
        transitions << id << ':' << int(p.status) << ':' << p.earned << ':' << p.activation << ':'
                    << p.elapsedMs << ':' << p.stage << ':' << p.choice << '\n';
        for (const auto &stage : p.path)
            transitions << stage << ',';
        for (const auto &[id, n] : p.counts)
            transitions << id << '=' << n << ';';
        transitions << '\n';
    }
    for (const auto &[source, n] : s.sequences)
        transitions << source << '=' << n << ';';
    for (const auto &receipt : s.receipts)
        transitions << receipt << ';';
    transitions << '\n';
    for (const auto &e : tx.effects()) {
        transitions << e.receipt << ':' << e.quest << ':' << e.type << '\n';
        fields(e.payload);
    }
    for (const auto &t : tx.trace())
        transitions << t.quest << ':' << t.action << ':' << t.sequence << '\n';
}

void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void reject(F action, std::optional<ErrorCode> code = {}) {
    try {
        action();
    } catch (const Error &e) {
        check(!code || e.code() == *code, "Wrong error code");
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::size_t apply(Runtime &r, Batch b, Budget budget = {}) {
    auto p = r.prepare(b, budget);
    check(p.status == PrepareStatus::Ready && bool(p.transaction), "Expected prepared transaction");
    auto n = p.transaction->effects().size();
    record(*p.transaction);
    check(r.commit(std::move(*p.transaction)), "Commit failed");
    return n;
}
void command(Runtime &r, CommandKind kind, std::string choice = {}, std::string id = "demo.delivery") {
    auto b = r.batch();
    b.commands.push_back({id, kind, r.view(id).activation, std::move(choice)});
    apply(r, std::move(b));
}
void deliver(Runtime &r, std::uint64_t seq, std::string item = "apple", std::int64_t weight = 2) {
    r.post(r.event("demo.delivery", "demo.delivered", "test.input", seq,
                   {{"item", std::move(item)}, {"weight", weight}}));
    apply(r, r.batch());
}
auto bytes(const std::shared_ptr<const Catalog> &c, const Runtime &r) {
    return encodeSnapshot(*c, r.state());
}
void content() {
    auto b = quest_example::bindings();
    auto d = quest_example::definition();
    auto c = Catalog::create(d, b);
    check(encodeCatalog(decodeCatalog(encodeCatalog(d), b)->definition()) == encodeCatalog(d),
          "Content roundtrip");
    auto bad = d;
    bad.quests.push_back(d.quests[0]);
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].prerequisites = {"missing"};
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].prerequisites = {"demo.followup"};
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].stages[0].transitions[0].next = "collect";
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].stages[1].transitions.clear();
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].stages[0].objectives[0].binding.key = "missing.handler";
    reject([&] { Catalog::create(bad, b); }, ErrorCode::MissingBinding);
    bad = d;
    bad.quests[0].stages[0].objectives[0].binding.version = 2;
    reject([&] { Catalog::create(bad, b); }, ErrorCode::MissingBinding);
    bad = d;
    bad.quests[0].stages[0].objectives[1].binding.parameters["minimum"] = std::string("bad");
    reject([&] { Catalog::create(bad, b); }, ErrorCode::TypeMismatch);
    bad = d;
    bad.quests[0].stages[0].objectives[1].binding.parameters["minimum"] = std::int64_t{0};
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].stages[0].objectives[0].mode = ObjectiveMode::Current;
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].stages[0].objectives[0].target = 0;
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.quests[0].effects.push_back(bad.quests[0].effects[0]);
    reject([&] { Catalog::create(bad, b); });
    bad = d;
    bad.facts["huge"] = std::string(1025, 'x');
    reject([&] { Catalog::create(bad, b); }, ErrorCode::Limit);
    bad = d;
    bad.quests[0].stages[1].choices = {"unknown"};
    reject([&] { Catalog::create(bad, b); });
    engine::ModuleComposition<Bindings> onlyBuiltins;
    auto builtin = moduleRegistration();
    onlyBuiltins.replace({&builtin, 1});
    reject([&] { decodeCatalog(encodeCatalog(d), onlyBuiltins.snapshot()); }, ErrorCode::MissingBinding);
    auto archive = encodeCatalog(d);
    archive[20] ^= 1;
    reject([&] { decodeCatalog(archive, b); });
    archive = encodeCatalog(d);
    archive.push_back(0);
    reject([&] { decodeCatalog(archive, b); });
    for (std::size_t n = 0; n < 20; ++n)
        reject([&] { decodeCatalog(std::span(archive).first(n), b); });
    check(c->bindings().objective(d.quests[0].stages[0].objectives[1].binding).schema.version == 1,
          "Custom module not retained");
}
std::vector<std::uint8_t> behavior() {
    auto c = quest_example::catalog();
    Runtime r(c), other(c);
    check(r.view("demo.followup").status == Status::Locked, "Prerequisite not locked");
    reject([&] { command(r, CommandKind::Activate, {}, "demo.followup"); });
    command(r, CommandKind::Activate);
    check(other.view("demo.delivery").status == Status::Available, "Shared mutable quest state");
    deliver(r, 1, "pear", 1);
    check(r.view("demo.delivery").counts.at("apples") == 0, "Event filter failed");
    deliver(r, 2, "apple", 1);
    check(r.view("demo.delivery").counts.at("heavy") == 0, "Custom objective filter failed");
    deliver(r, 3);
    check(r.view("demo.delivery").stage == "choose", "All objectives did not advance");
    reject([&] { command(r, CommandKind::Choose, "invalid"); });
    command(r, CommandKind::Choose, "friendly");
    check(r.view("demo.delivery").stage == "wait", "Choice branch failed");
    auto paused = r.batch();
    apply(r, paused);
    check(r.view("demo.delivery").elapsedMs == 0, "No delta advanced timer");
    auto tick = r.batch();
    tick.deltaMs = 499;
    apply(r, tick);
    check(r.view("demo.delivery").status == Status::Active, "Timer completed early");
    auto before = bytes(c, r);
    tick = r.batch();
    tick.deltaMs = 1;
    auto prepared = r.prepare(tick);
    check(prepared.transaction->effects().size() == 1 && bytes(c, r) == before,
          "Prepare mutated live reward/state");
    check(prepared.transaction->effects()[0].receipt == "demo.delivery/first-completion",
          "Receipt tied to wrong identity");
    // Host rejects the proposed award: discard the candidate and try again.
    prepared.transaction.reset();
    check(bytes(c, r) == before, "Rejected effect changed state");
    check(apply(r, tick) == 1, "Completion did not propose reward");
    check(r.view("demo.followup").status == Status::Available, "Completion did not unlock prerequisite");
    check(apply(r, r.batch()) == 0, "Repeated tick emitted reward");
    auto saved = bytes(c, r);
    auto decoded = decodeSnapshot(*c, saved);
    check(encodeSnapshot(*c, decoded) == saved, "Snapshot roundtrip");
    auto oldEvent = r.event("demo.delivery", "demo.delivered", "old.input", 1,
                            {{"item", std::string("apple")}, {"weight", std::int64_t{2}}});
    r.restore(decoded);
    check(r.state().generation == 2 && r.trace().empty(), "Restore did not rebase/clear diagnostics");
    reject([&] { r.post(oldEvent); }, ErrorCode::Stale);
    check(apply(r, r.batch()) == 0, "Restore replayed effects");
    command(r, CommandKind::Retry);
    deliver(r, 4);
    deliver(r, 5);
    command(r, CommandKind::Choose, "quiet");
    check(r.view("demo.delivery").stage == "gate" && r.view("demo.delivery").earned,
          "Practice lost durable completion");
    auto facts = r.batch();
    facts.facts = {{"gate", true}};
    check(apply(r, facts) == 0, "Practice duplicated effects");
    command(r, CommandKind::Activate, {}, "demo.followup");
    check(r.view("demo.followup").status == Status::Completed, "Initial current fact did not resolve");
    auto result = bytes(c, r);
    Runtime restored(c);
    restored.restore(decodeSnapshot(*c, result));
    check(restored.pending() == 0, "Snapshot retained transient queue");
    return result;
}
void queuesAndTransactions() {
    auto d = quest_example::definition();
    d.quests[0].stages[0].objectives[0].target = 1000000;
    auto c = Catalog::create(d, quest_example::bindings());
    Runtime r(c), foreign(c);
    command(r, CommandKind::Activate);
    command(foreign, CommandKind::Activate);
    auto e = r.event("demo.delivery", "demo.delivered", "test.input", 1,
                     {{"item", std::string("apple")}, {"weight", std::int64_t{2}}});
    reject([&] { foreign.post(e); }, ErrorCode::Stale);
    auto malformed = e;
    malformed.payload["weight"] = true;
    reject([&] { r.post(malformed); }, ErrorCode::TypeMismatch);
    r.post(e);
    reject([&] { r.post(e); }, ErrorCode::Duplicate);
    auto before = bytes(c, r);
    auto p = r.prepare(r.batch(), {64, 1});
    check(p.status == PrepareStatus::BudgetExceeded && !p.transaction && r.pending() == 1 &&
              bytes(c, r) == before,
          "Budget exhaustion changed state/queue");
    auto ready = r.prepare(r.batch());
    auto second = e;
    second.sequence = 2;
    r.post(second);
    check(!r.commit(std::move(*ready.transaction)), "Changed queue accepted stale candidate");
    apply(r, r.batch());
    reject([&] { r.post(e); }, ErrorCode::Duplicate);
    auto stale = r.batch();
    apply(r, r.batch());
    reject([&] { r.prepare(stale); }, ErrorCode::Stale);
    ready = r.prepare(r.batch());
    auto ready2 = r.prepare(r.batch());
    check(r.commit(std::move(*ready.transaction)), "First candidate commit");
    check(!r.commit(std::move(*ready2.transaction)), "Double candidate accepted");
    ready = r.prepare(r.batch());
    check(!foreign.commit(std::move(*ready.transaction)), "Foreign runtime accepted candidate");
    auto old = r.event("demo.delivery", "demo.delivered", "old.input", 1, e.payload);
    command(r, CommandKind::Retry);
    reject([&] { r.post(old); }, ErrorCode::Stale);
    for (std::uint64_t n = 3; n < 259; ++n)
        r.post(r.event("demo.delivery", "demo.delivered", "test.input", n, e.payload));
    reject([&] { r.post(r.event("demo.delivery", "demo.delivered", "test.input", 259, e.payload)); },
           ErrorCode::Limit);
    apply(r, r.batch(), {64, 4096});
    check(r.pending() == 192 && r.state().sequences.at("test.input") == 66, "FIFO bounded prefix failed");
    while (r.pending())
        apply(r, r.batch());
    auto snapshot = r.state();
    ready = r.prepare(r.batch());
    r.restore(snapshot);
    check(!r.commit(std::move(*ready.transaction)), "Restore did not invalidate candidate");
    for (int n = 0; n < 63; ++n)
        r.post(r.event("demo.delivery", "demo.delivered", "source.s" + std::to_string(n), 1, e.payload));
    reject([&] { r.post(r.event("demo.delivery", "demo.delivered", "source.overflow", 1, e.payload)); },
           ErrorCode::Limit);
}
void snapshotsAndScopes() {
    auto c = quest_example::catalog();
    Runtime r(c);
    command(r, CommandKind::Activate);
    auto payload = Fields{{"item", std::string("apple")}, {"weight", std::int64_t{2}}};
    for (std::uint64_t seq = 1; seq <= 3; ++seq)
        r.post(r.event("demo.delivery", "demo.delivered", "source.test", seq, payload));
    apply(r, r.batch());
    check(r.view("demo.delivery").stage == "choose" && r.state().sequences.at("source.test") == 3,
          "Queued stage scope consumption failed");
    check(r.trace().back().action == "discarded-scope", "Old-stage queued event spilled into new stage");
    auto before = bytes(c, r);
    auto bad = r.state();
    bad.quests.at("demo.delivery").counts["fake"] = 1;
    reject([&] { r.restore(bad); });
    check(bytes(c, r) == before, "Invalid restore changed live state");
    bad = r.state();
    bad.quests.erase("demo.delivery");
    bad.quests.emplace("unknown", Progress{});
    reject([&] { r.restore(bad); }, ErrorCode::InvalidSnapshot);
    bad = r.state();
    bad.receipts.insert("unknown/receipt");
    reject([&] { r.restore(bad); });
    bad = r.state();
    bad.fingerprint = "different";
    reject([&] { r.restore(bad); });
    bad = r.state();
    bad.quests.at("demo.delivery").path = {"choose"};
    reject([&] { r.restore(bad); });
    bad = r.state();
    bad.quests.at("demo.followup").status = Status::Available;
    reject([&] { r.restore(bad); });
    auto data = before;
    data[data.size() / 2] ^= 1;
    reject([&] { decodeSnapshot(*c, data); });
    for (std::size_t n = 0; n < before.size(); n += 7)
        reject([&] { decodeSnapshot(*c, std::span(before).first(n)); });
    auto b = r.batch();
    b.facts["gate"] = std::string("wrong");
    reject([&] { r.prepare(b); }, ErrorCode::TypeMismatch);
    command(r, CommandKind::Cancel);
    check(r.view("demo.delivery").status == Status::Cancelled, "Cancel failed");
    command(r, CommandKind::Retry);
    command(r, CommandKind::Fail);
    check(r.view("demo.delivery").status == Status::Failed, "Fail failed");
    command(r, CommandKind::Retry);
    deliver(r, 1);
    deliver(r, 2);
    command(r, CommandKind::Choose, "friendly");
    b = r.batch();
    b.deltaMs = 500;
    b.replay = true;
    check(apply(r, b) == 0 && r.view("demo.delivery").earned && r.state().receipts.size() == 1,
          "Replay emitted or lost receipts");
    check(apply(r, r.batch()) == 0, "Replay receipt not durable");
}
void boundsAndOrdering() {
    auto d = quest_example::definition();
    auto b = quest_example::bindings();
    // Both guards are true: authored order selects the first branch.
    d.quests[0].stages[1].transitions[0].condition = {"quest.always", 1, {}};
    d.quests[0].stages[1].transitions[1].condition = {"quest.always", 1, {}};
    auto c = Catalog::create(d, b);
    Runtime r(c);
    command(r, CommandKind::Activate);
    auto patch = r.batch();
    patch.facts = {{"gate", true}};
    apply(r, patch);
    check(r.view("demo.delivery").counts.at("apples") == 0, "Fact update incremented occurrence objective");
    deliver(r, 1);
    deliver(r, 2);
    command(r, CommandKind::Choose, "quiet");
    check(r.view("demo.delivery").stage == "wait", "Branch order is nondeterministic");
    auto saved = r.state();
    saved.quests.at("demo.delivery").elapsedMs = Limits::maxTimeMs;
    r.restore(saved);
    auto before = bytes(c, r);
    auto tick = r.batch();
    tick.deltaMs = 1;
    reject([&] { r.prepare(tick); }, ErrorCode::Limit);
    check(bytes(c, r) == before, "Timer overflow mutated state");
    saved = r.state();
    saved.generation = std::numeric_limits<std::uint64_t>::max() - 2;
    reject([&] { r.restore(saved); }, ErrorCode::Limit);
    check(bytes(c, r) == before, "Generation overflow mutated state");
    // Aggregate limits stop a catalogue even when every individual quest is legal.
    auto big = quest_example::definition();
    big.quests.clear();
    for (int i = 0; i < 820; ++i) {
        auto q = d.quests[0];
        q.id = "q" + std::to_string(i);
        q.stages[0].objectives.clear();
        for (int j = 0; j < 10; ++j) {
            auto o = d.quests[0].stages[0].objectives[0];
            o.id = "o" + std::to_string(j);
            q.stages[0].objectives.push_back(o);
        }
        big.quests.push_back(std::move(q));
    }
    reject([&] { Catalog::create(big, b); }, ErrorCode::Limit);
}

void providerLifetime() {
    std::weak_ptr<int> weak;
    std::shared_ptr<const Catalog> catalog;
    {
        auto provider = std::make_shared<int>(7);
        weak = provider;
        engine::ModuleComposition<Bindings> composition;
        auto custom = quest_example::customRegistration();
        auto install = custom.install;
        custom.install = [provider, install](Bindings &bindings) { install(bindings); };
        std::vector<engine::ModuleRegistration<Bindings>> modules{custom, moduleRegistration()};
        catalog = Catalog::create(quest_example::definition(), composition.replace(modules));
    }
    check(!weak.expired(), "Catalogue lost module provider");
    {
        Runtime runtime(catalog);
        catalog.reset();
        check(!weak.expired(), "Runtime lost module provider");
        command(runtime, CommandKind::Activate);
        deliver(runtime, 1);
    }
    check(weak.expired(), "Module provider leaked after last runtime");
}

void callbacks() {
    Runtime *runtime = nullptr;
    int mode = 0;
    engine::ModuleComposition<Bindings> composition;
    auto builtins = moduleRegistration();
    auto custom = quest_example::customRegistration();
    auto callback = custom;
    callback.descriptor.id = "test.callback";
    callback.descriptor.schemas.clear();
    callback.install = [&](Bindings &b) {
        b.addObjective("test.callback",
                       {{1, {}, {}}, ObjectiveMode::Current, [&](const Evaluation &, const Fields &) {
                            if (mode == 1)
                                throw std::runtime_error("callback failed");
                            if (mode == 2)
                                runtime->prepare(runtime->batch());
                            if (mode == 3)
                                runtime->restore(runtime->state());
                            return 0u;
                        }});
    };
    std::vector<engine::ModuleRegistration<Bindings>> regs{builtins, custom, callback};
    auto bindings = composition.replace(regs);
    auto d = quest_example::definition();
    d.quests[0].stages[0].objectives.push_back({"custom", ObjectiveMode::Current, {"test.callback", 1, {}}});
    auto c = Catalog::create(d, bindings);
    Runtime r(c);
    runtime = &r;
    command(r, CommandKind::Activate);
    auto before = bytes(c, r);
    mode = 1;
    try {
        r.prepare(r.batch());
        throw std::logic_error("Callback unexpectedly succeeded");
    } catch (const std::runtime_error &e) {
        check(std::string(e.what()) == "callback failed", "Wrong callback exception");
    }
    check(bytes(c, r) == before, "Callback exception changed state");
    mode = 2;
    reject([&] { r.prepare(r.batch()); }, ErrorCode::Reentrant);
    mode = 3;
    reject([&] { r.prepare(r.batch()); }, ErrorCode::Reentrant);
    mode = 0;
    apply(r, r.batch());
}
} // namespace
int main(int argc, char **argv) {
    try {
        content();
        auto result = behavior();
        queuesAndTransactions();
        snapshotsAndScopes();
        boundsAndOrdering();
        providerLifetime();
        callbacks();
        if (argc == 3 && std::string(argv[1]) == "--write") {
            std::ofstream f(argv[2], std::ios::binary);
            f.write(reinterpret_cast<const char *>(result.data()), std::streamsize(result.size()));
            check(bool(f), "Cannot write cross-runtime snapshot");
            std::ofstream log(std::string(argv[2]) + ".trace", std::ios::binary);
            log << transitions.str();
            check(bool(log), "Cannot write transition trace");
        } else if (argc == 3 && std::string(argv[1]) == "--verify") {
            std::ifstream f(argv[2], std::ios::binary);
            std::vector<std::uint8_t> external{std::istreambuf_iterator<char>(f), {}};
            check(external == result, "Native/Wasm snapshot trace mismatch");
            decodeSnapshot(*quest_example::catalog(), external);
            std::ifstream log(std::string(argv[2]) + ".trace", std::ios::binary);
            std::string actual{std::istreambuf_iterator<char>(log), {}};
            check(actual == transitions.str(), "Native/Wasm intermediate transition trace mismatch");
        } else {
            check(argc == 1, "Use --write/--verify path");
            check(!transitions.str().empty(), "Missing transition trace");
        }
        std::uint64_t digest = 14695981039346656037ULL;
        for (auto byte : result) {
            digest ^= byte;
            digest *= 1099511628211ULL;
        }
        std::cout << "quests: " << checks << " checks passed; snapshot=" << std::hex << digest
                  << " bytes=" << std::dec << result.size() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "quests: " << e.what() << '\n';
        return 1;
    }
}
