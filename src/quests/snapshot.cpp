#include "internal.hpp"
#include <bit>

namespace engine::quests {
using namespace detail;
namespace {
std::uint64_t hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t h = 14695981039346656037ULL;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}
struct Writer {
    std::vector<std::uint8_t> data;
    void u8(std::uint8_t n) {
        bound(data.size() + 1, Limits::bytes, "Wire byte limit");
        data.push_back(n);
    }
    void u32(std::uint32_t n) {
        for (int i = 0; i < 4; ++i)
            u8(std::uint8_t(n >> (i * 8)));
    }
    void u64(std::uint64_t n) {
        for (int i = 0; i < 8; ++i)
            u8(std::uint8_t(n >> (i * 8)));
    }
    void text(std::string_view s) {
        bound(s.size(), Limits::stringBytes, "Wire string limit");
        u32(std::uint32_t(s.size()));
        for (unsigned char c : s)
            u8(c);
    }
    void fields(const Fields &f) {
        u32(std::uint32_t(f.size()));
        for (const auto &[k, v] : f) {
            text(k);
            u8(std::uint8_t(v.index()));
            if (auto b = std::get_if<bool>(&v))
                u8(*b ? 1 : 0);
            else if (auto n = std::get_if<std::int64_t>(&v))
                u64(std::bit_cast<std::uint64_t>(*n));
            else
                text(std::get<std::string>(v));
        }
    }
    void call(const BindingCall &c) {
        text(c.key);
        u32(c.version);
        fields(c.parameters);
    }
    void header(std::string_view magic) {
        for (unsigned char c : magic)
            u8(c);
        u32(1);
    }
    std::vector<std::uint8_t> finish() {
        const auto sum = hash(data);
        u64(sum);
        return std::move(data);
    }
};
struct Reader {
    std::span<const std::uint8_t> data;
    std::size_t at = 0, fieldCount = 0;
    ErrorCode code;
    Reader(std::span<const std::uint8_t> bytes, std::string_view magic, ErrorCode c) : code(c) {
        bound(bytes.size(), Limits::bytes, "Wire byte limit");
        require(bytes.size() >= 20, code, "Truncated quest archive");
        std::uint64_t sum = 0;
        for (int i = 0; i < 8; ++i)
            sum |= std::uint64_t(bytes[bytes.size() - 8 + i]) << (8 * i);
        data = bytes.first(bytes.size() - 8);
        require(hash(data) == sum, code, "Quest archive checksum mismatch");
        for (unsigned char ch : magic)
            require(u8() == ch, code, "Unknown quest archive");
        require(u32() == 1, code, "Unsupported quest archive version");
    }
    std::uint8_t u8() {
        require(at < data.size(), code, "Truncated quest field");
        return data[at++];
    }
    std::uint32_t u32() {
        std::uint32_t n = 0;
        for (int i = 0; i < 4; ++i)
            n |= std::uint32_t(u8()) << (8 * i);
        return n;
    }
    std::uint64_t u64() {
        std::uint64_t n = 0;
        for (int i = 0; i < 8; ++i)
            n |= std::uint64_t(u8()) << (8 * i);
        return n;
    }
    std::uint32_t count(std::size_t max) {
        auto n = u32();
        bound(n, max, "Wire count limit");
        return n;
    }
    std::string text(std::size_t max = Limits::keyBytes) {
        auto n = count(max);
        require(n <= data.size() - at, code, "Truncated string");
        std::string s(reinterpret_cast<const char *>(data.data() + at), n);
        at += n;
        return s;
    }
    bool boolean() {
        const auto b = u8();
        require(b <= 1, code, "Invalid boolean");
        return b != 0;
    }
    Fields fields(std::size_t max = Limits::fields) {
        Fields f;
        auto n = count(max);
        fieldCount += n;
        bound(fieldCount, Limits::totalFields, "Wire field limit");
        while (n--) {
            auto k = text();
            Value v;
            switch (u8()) {
            case 0:
                v = boolean();
                break;
            case 1:
                v = std::bit_cast<std::int64_t>(u64());
                break;
            case 2:
                v = text(Limits::stringBytes);
                break;
            default:
                fail(code, "Invalid value tag");
            }
            require(f.emplace(std::move(k), std::move(v)).second, code, "Duplicate wire field");
        }
        return f;
    }
    BindingCall call() {
        BindingCall c;
        c.key = text();
        c.version = u32();
        c.parameters = fields();
        return c;
    }
    void end() { require(at == data.size(), code, "Trailing quest archive bytes"); }
};
bool completeGroup(const Progress &p, const Stage &stage) {
    auto done = [&](const auto &o) { return p.counts.at(o.id) == o.target; };
    return stage.group == Group::All ? std::all_of(stage.objectives.begin(), stage.objectives.end(), done)
                                     : std::any_of(stage.objectives.begin(), stage.objectives.end(), done);
}
} // namespace
std::vector<std::uint8_t> encodeCatalog(const CatalogDefinition &d) {
    validateDefinition(d, nullptr);
    Writer w;
    w.header("WEQCAT\r\n");
    w.text(d.id);
    w.text(d.content);
    w.fields(d.facts);
    w.u32(std::uint32_t(d.events.size()));
    for (const auto &[id, f] : d.events) {
        w.text(id);
        w.fields(f);
    }
    w.u32(std::uint32_t(d.quests.size()));
    for (const auto &q : d.quests) {
        w.text(q.id);
        w.text(q.content);
        w.text(q.entry);
        w.u32(std::uint32_t(q.prerequisites.size()));
        for (const auto &p : q.prerequisites)
            w.text(p);
        w.u32(std::uint32_t(q.stages.size()));
        for (const auto &s : q.stages) {
            w.text(s.id);
            w.u8(std::uint8_t(s.group));
            w.u32(std::uint32_t(s.objectives.size()));
            for (const auto &o : s.objectives) {
                w.text(o.id);
                w.u8(std::uint8_t(o.mode));
                w.call(o.binding);
                w.u32(o.target);
            }
            w.u32(std::uint32_t(s.choices.size()));
            for (const auto &c : s.choices)
                w.text(c);
            w.u32(std::uint32_t(s.transitions.size()));
            for (const auto &t : s.transitions) {
                w.call(t.condition);
                w.text(t.next);
                w.u8(std::uint8_t(t.outcome));
            }
        }
        w.u32(std::uint32_t(q.effects.size()));
        for (const auto &e : q.effects) {
            w.text(e.key);
            w.text(e.type);
            w.fields(e.payload);
        }
    }
    return w.finish();
}
std::shared_ptr<const Catalog> decodeCatalog(std::span<const std::uint8_t> bytes, BindingSnapshot bindings) {
    Reader r(bytes, "WEQCAT\r\n", ErrorCode::InvalidContent);
    CatalogDefinition d;
    d.id = r.text();
    d.content = r.text();
    d.facts = r.fields(Limits::facts);
    for (auto n = r.count(Limits::eventTypes); n--;) {
        auto id = r.text();
        auto f = r.fields();
        require(d.events.emplace(std::move(id), std::move(f)).second, ErrorCode::InvalidContent,
                "Duplicate event schema");
    }
    std::size_t stages = 0, objectives = 0, branches = 0;
    for (auto n = r.count(Limits::quests); n--;) {
        QuestDefinition q;
        q.id = r.text();
        q.content = r.text();
        q.entry = r.text();
        for (auto p = r.count(64); p--;)
            q.prerequisites.push_back(r.text());
        auto stageCount = r.count(Limits::stagesPerQuest);
        stages += stageCount;
        bound(stages, Limits::totalStages, "Wire stage limit");
        std::size_t qo = 0;
        while (stageCount--) {
            Stage s;
            s.id = r.text();
            s.group = Group(r.u8());
            auto count = r.count(Limits::objectivesPerQuest);
            qo += count;
            objectives += count;
            bound(qo, Limits::objectivesPerQuest, "Quest objective limit");
            bound(objectives, Limits::totalObjectives, "Wire objective limit");
            while (count--) {
                Objective o;
                o.id = r.text();
                o.mode = ObjectiveMode(r.u8());
                o.binding = r.call();
                o.target = r.u32();
                s.objectives.push_back(std::move(o));
            }
            for (auto c = r.count(Limits::choices); c--;)
                s.choices.push_back(r.text());
            count = r.count(Limits::branches);
            branches += count;
            bound(branches, Limits::totalBranches, "Wire branch limit");
            while (count--) {
                Transition t;
                t.condition = r.call();
                t.next = r.text();
                t.outcome = Outcome(r.u8());
                s.transitions.push_back(std::move(t));
            }
            q.stages.push_back(std::move(s));
        }
        for (auto e = r.count(Limits::effectsPerQuest); e--;) {
            EffectDefinition effect;
            effect.key = r.text();
            effect.type = r.text();
            effect.payload = r.fields();
            q.effects.push_back(std::move(effect));
        }
        d.quests.push_back(std::move(q));
    }
    r.end();
    return Catalog::create(std::move(d), std::move(bindings));
}
namespace detail {
std::string fingerprint(const CatalogDefinition &d) {
    auto bytes = encodeCatalog(d);
    auto h = hash(bytes);
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[i] = "0123456789abcdef"[h & 15];
        h >>= 4;
    }
    return out;
}
} // namespace detail
void validateSnapshot(const Catalog &c, const Snapshot &s) {
    const auto bad = ErrorCode::InvalidSnapshot;
    require(s.catalog == c.definition().id && s.content == c.definition().content &&
                s.fingerprint == c.fingerprint(),
            bad, "Catalogue/content identity mismatch");
    require(s.generation > 0 && s.generation < std::numeric_limits<std::uint64_t>::max() - 1 &&
                s.revision < std::numeric_limits<std::uint64_t>::max() - 1,
            bad, "Invalid saved generation/revision");
    fields(s.facts, Limits::facts);
    typed(s.facts, c.definition().facts, false);
    require(s.quests.size() == c.definition().quests.size(), bad, "Saved quest count mismatch");
    for (const auto &q : c.definition().quests)
        require(s.quests.contains(q.id), bad, "Missing saved quest");
    std::set<std::string> expectedReceipts;
    for (const auto &q : c.definition().quests) {
        auto found = s.quests.find(q.id);
        require(found != s.quests.end(), bad, "Missing saved quest");
        const auto &p = found->second;
        require(p.status >= Status::Locked && p.status <= Status::Cancelled, bad, "Invalid saved status");
        require(p.elapsedMs <= Limits::maxTimeMs &&
                    p.activation < std::numeric_limits<std::uint64_t>::max() - 1,
                bad, "Invalid saved activation/timer");
        if (p.status == Status::Locked || p.status == Status::Available) {
            require(!p.earned && p.activation == 0 && p.elapsedMs == 0 && p.stage.empty() &&
                        p.choice.empty() && p.path.empty() && p.counts.empty(),
                    bad, "Inactive quest has attempt state");
            require((p.status == Status::Available) == unlocked(s, q), bad,
                    "Saved availability disagrees with prerequisites");
            continue;
        }
        require(unlocked(s, q), bad, "Saved attempt predates prerequisites");
        require(p.activation > 0 && !p.path.empty() && p.path.size() <= q.stages.size() &&
                    p.path.front() == q.entry && p.path.back() == p.stage,
                bad, "Invalid saved stage path");
        std::set<std::string> visited;
        for (std::size_t i = 0; i < p.path.size(); ++i) {
            require(visited.insert(p.path[i]).second, bad, "Repeated saved stage");
            const auto &stage = c.stage(q.id, p.path[i]);
            if (i + 1 < p.path.size())
                require(std::any_of(stage.transitions.begin(), stage.transitions.end(),
                                    [&](const auto &t) { return t.next == p.path[i + 1]; }),
                        bad, "Impossible saved stage edge");
        }
        const auto &stage = c.stage(q.id, p.stage);
        require(p.choice.empty() ||
                    std::find(stage.choices.begin(), stage.choices.end(), p.choice) != stage.choices.end(),
                bad, "Unknown saved choice");
        require(p.counts.size() == stage.objectives.size(), bad, "Saved objective count mismatch");
        for (const auto &o : stage.objectives) {
            auto i = p.counts.find(o.id);
            require(i != p.counts.end() && i->second <= o.target, bad, "Invalid objective progress");
        }
        if (p.status == Status::Completed) {
            require(p.earned && completeGroup(p, stage), bad, "Invalid saved completion");
            require(stage.transitions.empty() ||
                        std::any_of(
                            stage.transitions.begin(), stage.transitions.end(),
                            [](const auto &t) { return t.next.empty() && t.outcome == Outcome::Complete; }),
                    bad, "Nonterminal saved completion");
        }
        if (p.earned)
            for (const auto &effect : q.effects)
                expectedReceipts.insert(receipt(q, effect));
    }
    require(s.receipts == expectedReceipts, bad, "Receipt ledger disagrees with earned completion");
    bound(s.sequences.size(), Limits::sources, "Saved source limit");
    for (const auto &[source, sequence] : s.sequences) {
        key(source);
        require(sequence > 0 && sequence < std::numeric_limits<std::uint64_t>::max() - 1, bad,
                "Invalid saved source sequence");
    }
}
std::vector<std::uint8_t> encodeSnapshot(const Catalog &c, const Snapshot &s) {
    validateSnapshot(c, s);
    Writer w;
    w.header("WEQSAV\r\n");
    w.text(s.catalog);
    w.text(s.content);
    w.text(s.fingerprint);
    w.u64(s.generation);
    w.u64(s.revision);
    w.fields(s.facts);
    w.u32(std::uint32_t(s.quests.size()));
    for (const auto &[id, p] : s.quests) {
        w.text(id);
        w.u8(std::uint8_t(p.status));
        w.u8(p.earned ? 1 : 0);
        w.u64(p.activation);
        w.u64(p.elapsedMs);
        w.text(p.stage);
        w.text(p.choice);
        w.u32(std::uint32_t(p.path.size()));
        for (const auto &stage : p.path)
            w.text(stage);
        w.u32(std::uint32_t(p.counts.size()));
        for (const auto &[key, value] : p.counts) {
            w.text(key);
            w.u32(value);
        }
    }
    w.u32(std::uint32_t(s.sequences.size()));
    for (const auto &[id, seq] : s.sequences) {
        w.text(id);
        w.u64(seq);
    }
    w.u32(std::uint32_t(s.receipts.size()));
    for (const auto &receipt : s.receipts)
        w.text(receipt);
    return w.finish();
}
Snapshot decodeSnapshot(const Catalog &c, std::span<const std::uint8_t> bytes) {
    Reader r(bytes, "WEQSAV\r\n", ErrorCode::InvalidSnapshot);
    Snapshot s;
    s.catalog = r.text();
    s.content = r.text();
    s.fingerprint = r.text();
    s.generation = r.u64();
    s.revision = r.u64();
    s.facts = r.fields(Limits::facts);
    for (auto n = r.count(Limits::quests); n--;) {
        auto id = r.text();
        Progress p;
        p.status = Status(r.u8());
        p.earned = r.boolean();
        p.activation = r.u64();
        p.elapsedMs = r.u64();
        p.stage = r.text();
        p.choice = r.text();
        for (auto count = r.count(Limits::stagesPerQuest); count--;)
            p.path.push_back(r.text());
        for (auto count = r.count(Limits::objectivesPerQuest); count--;) {
            auto key = r.text();
            auto value = r.u32();
            require(p.counts.emplace(std::move(key), value).second, ErrorCode::InvalidSnapshot,
                    "Duplicate progress field");
        }
        require(s.quests.emplace(std::move(id), std::move(p)).second, ErrorCode::InvalidSnapshot,
                "Duplicate saved quest");
    }
    for (auto n = r.count(Limits::sources); n--;) {
        auto id = r.text();
        auto seq = r.u64();
        require(s.sequences.emplace(std::move(id), seq).second, ErrorCode::InvalidSnapshot,
                "Duplicate saved source");
    }
    for (auto n = r.count(Limits::quests * Limits::effectsPerQuest); n--;)
        require(s.receipts.insert(r.text(Limits::keyBytes * 2 + 1)).second, ErrorCode::InvalidSnapshot,
                "Duplicate receipt");
    r.end();
    validateSnapshot(c, s);
    return s;
}
} // namespace engine::quests
