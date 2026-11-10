#include "daw/coop/Messages.h"
#include "daw/coop/Wire.h"

#include "daw/model/ChangeRecorder.h"
#include "daw/model/FormatVersion.h"
#include "daw/model/MidiClip.h"
#include "daw/model/NetworkProtocol.h"
#include "daw/model/Project.h"
#include "daw/model/SharedProject.h"

#include "../support/RandomProjectEdits.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw;

namespace
{

const juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("daw-message-tests-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));

juce::String hexOf(const juce::MemoryBlock& block)
{
    return juce::String::toHexString(block.getData(), static_cast<int>(block.getSize()), 0);
}

bool sameChanges(const model::ChangeSet& a, const model::ChangeSet& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t index = 0; index < a.size(); ++index)
    {
        const auto& x = a[index];
        const auto& y = b[index];
        const auto sameOptional = [](const std::optional<juce::var>& p,
                                     const std::optional<juce::var>& q)
        { return p.has_value() == q.has_value() && (!p || model::isSameValue(*p, *q)); };
        if (x.kind != y.kind || x.path != y.path || x.property != y.property
            || !sameOptional(x.before, y.before) || !sameOptional(x.after, y.after)
            || x.index != y.index || x.newIndex != y.newIndex
            || !test::isExactlyTheSame(x.child, y.child))
            return false;
    }
    return true;
}

coop::AcceptedChange roundTrip(const coop::AcceptedChange& change)
{
    const auto decoded = coop::decodeMessage(coop::encode(change));
    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<coop::AcceptedChange>(*decoded));
    return std::get<coop::AcceptedChange>(*decoded);
}

} // namespace

TEST_CASE("Values and trees cross the network exactly", "[coop]")
{
    juce::ValueTree tree("Thing");
    tree.setProperty("int", 7, nullptr);
    tree.setProperty("negative", -123456, nullptr);
    tree.setProperty("int64", juce::int64{1} << 50, nullptr);
    tree.setProperty("double", 0.1, nullptr);
    tree.setProperty("minusZero", -0.0, nullptr);
    tree.setProperty("bool", false, nullptr);
    tree.setProperty("text", juce::String(juce::CharPointer_UTF8("caf\xc3\xa9 \xe2\x99\xab")),
                     nullptr);
    tree.setProperty("binary", juce::MemoryBlock("\0\1\2\3", 4), nullptr);
    juce::Array<juce::var> array{1, "two", 3.0};
    tree.setProperty("array", array, nullptr);
    tree.appendChild(juce::ValueTree("Child"), nullptr);

    coop::Writer writer;
    writer.writeTree(tree);
    coop::Reader reader(writer.getData());
    const auto back = reader.readTree();
    CHECK(reader.isOk());
    CHECK(reader.isAtEnd());
    CHECK(test::isExactlyTheSame(back, tree));
}

TEST_CASE("Changes of random edits cross the network exactly", "[coop]")
{
    model::Project project;
    model::ChangeRecorder recorder(project.getState());
    juce::UndoManager undoManager;
    test::RandomProjectEditor editor(11, folder);

    for (int edit = 0; edit < 1000; ++edit)
    {
        [[maybe_unused]] const auto what = editor.edit(project, &undoManager);
        if (edit % 5 != 4)
            continue;

        coop::AcceptedChange message;
        message.sequence = static_cast<std::uint64_t>(edit);
        message.author = 3;
        message.clientSequence = 99;
        message.name = "Edit";
        message.undoStep = 12;
        message.changes = model::sharedChanges(recorder.takeChanges(), folder);
        const auto back = roundTrip(message);
        CHECK(back.sequence == message.sequence);
        CHECK(back.author == 3);
        CHECK(sameChanges(back.changes, message.changes));
    }
}

TEST_CASE("Changed notes travel as the range that changed", "[coop]")
{
    model::Project project;
    auto track = project.addTrack("Keys", model::TrackKind::midi);
    std::vector<model::Note> notes;
    for (int index = 0; index < 1000; ++index)
        notes.push_back({index + 1, index * 1000LL, 500, 60 + index % 12, 100});
    track.addMidiClip("Clip", {0, 0, 2'000'000}, notes);

    model::ChangeRecorder recorder(project.getState());
    notes[500].key += 1;
    model::MidiClip(track.getClip(0).getState()).setNotes(notes);
    const auto changes = recorder.takeChanges();
    REQUIRE(changes.size() == 1);

    coop::Writer writer;
    writer.writeChange(changes[0]);
    const auto withBefore = changes[0].before->getBinaryData()->getSize();
    // The message carries the old notes whole (to check them) and only a few bytes of the
    // new ones.
    CHECK(writer.getData().getSize() < withBefore + 200);

    coop::Reader reader(writer.getData());
    const auto back = reader.readChange();
    REQUIRE(reader.isOk());
    CHECK(model::isSameValue(*back.after, *changes[0].after));
}

TEST_CASE("Malformed messages are refused without harm", "[coop]")
{
    model::Project project;
    model::ChangeRecorder recorder(project.getState());
    juce::UndoManager undoManager;
    test::RandomProjectEditor editor(5, folder);
    for (int edit = 0; edit < 100; ++edit)
        [[maybe_unused]] const auto what = editor.edit(project, &undoManager);

    coop::Transaction transaction;
    transaction.clientSequence = 1;
    transaction.changes = model::sharedChanges(recorder.takeChanges(), folder);
    const auto encoded = coop::encode(transaction);
    REQUIRE(coop::decodeMessage(encoded).has_value());

    juce::Random random(17);
    for (int attempt = 0; attempt < 2000; ++attempt)
    {
        auto damaged = encoded;
        if (attempt % 2 == 0)
        {
            damaged.setSize(static_cast<std::size_t>(random.nextInt(
                static_cast<int>(encoded.getSize()))));
        }
        else
        {
            const auto at = static_cast<std::size_t>(
                random.nextInt(static_cast<int>(encoded.getSize())));
            damaged[at] = static_cast<char>(random.nextInt(256));
        }
        // Either refused or a message: never a crash or a huge allocation.
        [[maybe_unused]] const auto decoded = coop::decodeMessage(damaged);
    }

    CHECK_FALSE(coop::decodeMessage({}).has_value());
    CHECK_FALSE(coop::decodeMessage(juce::MemoryBlock("\xff", 1)).has_value());

    // A count far beyond the data is refused before anything is allocated for it.
    coop::Writer huge;
    huge.writeByte(static_cast<std::uint8_t>(coop::Message(coop::ParticipantList{}).index()));
    huge.writeVarUInt(std::uint64_t{1} << 60);
    CHECK_FALSE(coop::decodeMessage(huge.getData()).has_value());
}

TEST_CASE("Snapshots are compressed and checked", "[coop]")
{
    model::Project project;
    for (int index = 0; index < 50; ++index)
        project.addTrack("Track " + juce::String(index)).getEffects().add("equaliser", 0);

    coop::ProjectSnapshot snapshot;
    snapshot.sequence = 42;
    snapshot.project = model::sharedCopy(project.getState(), folder);
    snapshot.files.push_back({"Audio/1/take.wav", juce::MemoryBlock(32, true), 1234});
    const auto encoded = coop::encode(snapshot);

    coop::Writer plain;
    plain.writeTree(snapshot.project);
    CHECK(encoded.getSize() < plain.getData().getSize() / 4);

    const auto decoded = coop::decodeMessage(encoded);
    REQUIRE(decoded.has_value());
    const auto& back = std::get<coop::ProjectSnapshot>(*decoded);
    CHECK(back.sequence == 42);
    CHECK(test::isExactlyTheSame(back.project, snapshot.project));
    CHECK(back.files == snapshot.files);
}

TEST_CASE("Every message survives a round trip", "[coop]")
{
    const std::vector<coop::Message> messages{
        coop::LockRequest{model::EditScope::track(5), true},
        coop::RecordingState{true},
        coop::TransportRequest{true, 123456},
        coop::HostCommand{coop::HostCommand::Kind::remove, 4},
        coop::Leave{},
        coop::ParticipantList{{{1, "Ann", 0, true, true, false}, {2, "Marco", 3, false, false, true}}},
        coop::LockState{{{model::EditScope::bus(9), 2}, {model::EditScope::project(), 0}}, true},
        coop::Rejection{7, coop::RejectionReason::locked, 2, "Track 5"},
        coop::TransportStart{2, true, 99, 12.5},
        coop::Invite{"ABCDE-FGHJK", 86400.0},
        coop::FileAvailable{juce::MemoryBlock(32, true)},
        coop::SessionEnded{coop::SessionEnded::Reason::removed},
        coop::PresenceUpdate{3, {7, true, 7, 1000, 8, 64, 2000, {1, 2}, {5, 6}, true, 3000}},
        coop::Ping{1.25},
        coop::Pong{1.25, 100.5},
    };

    for (const auto& message : messages)
    {
        const auto encoded = coop::encode(message);
        const auto decoded = coop::decodeMessage(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->index() == message.index());
        CHECK(coop::encode(*decoded) == encoded);
    }

    CHECK(coop::isUnreliable(coop::Ping{}));
    CHECK_FALSE(coop::isUnreliable(coop::Transaction{}));
}

TEST_CASE("The handshake's frames have fixed bytes", "[coop]")
{
    coop::Hello hello;
    hello.protocolVersion = 1;
    hello.formatVersion = 6;
    hello.appVersion = "0.9.0";
    hello.request = coop::JoinSession{"ABCDE-FGHJK", "Ann"};
    hello.delivered = 0;
    const auto helloFrame = coop::encodeFrame(hello);
    CHECK(hexOf(helloFrame)
          == "014d49434e0100000006000000"
             "05302e392e30"
             "01"
             "0b41424344452d4647484a4b"
             "03416e6e"
             "00");

    const auto decoded = coop::decodeHello(helloFrame);
    REQUIRE(decoded.has_value());
    CHECK(std::get<coop::JoinSession>(decoded->request).inviteCode == "ABCDE-FGHJK");

    // Whatever comes after the versions, any version can read them.
    auto newer = helloFrame;
    newer.append("future fields", 13);
    const auto versions = coop::readHelloVersions(newer);
    REQUIRE(versions.has_value());
    CHECK(versions->protocolVersion == 1);
    CHECK(versions->formatVersion == 6);
    CHECK_FALSE(coop::readHelloVersions(juce::MemoryBlock("\x01GET /", 6)).has_value());

    coop::Refused refused;
    refused.reason = coop::RefusalReason::incompatibleProtocol;
    refused.serverProtocol = 2;
    refused.serverFormat = 7;
    refused.detail = "x";
    const auto refusedFrame = coop::encodeFrame(refused);
    CHECK(hexOf(refusedFrame) == "0300020000000700000001" "78");
    const auto back = coop::decodeRefused(refusedFrame);
    REQUIRE(back.has_value());
    CHECK(back->serverProtocol == 2);
}

TEST_CASE("Peers of other versions are told what to do", "[coop]")
{
    coop::Refused newerServer;
    newerServer.reason = coop::RefusalReason::incompatibleProtocol;
    newerServer.serverProtocol = static_cast<std::uint32_t>(model::networkProtocolVersion + 1);
    CHECK(coop::describe(newerServer).contains("Check for Updates"));

    coop::Refused olderServer = newerServer;
    olderServer.serverProtocol = static_cast<std::uint32_t>(model::networkProtocolVersion - 1);
    CHECK(coop::describe(olderServer).contains("older version"));

    coop::Refused format;
    format.reason = coop::RefusalReason::incompatibleProjectFormat;
    format.serverFormat = static_cast<std::uint32_t>(model::currentFormatVersion + 1);
    CHECK(coop::describe(format).contains("newer version"));

    // A reason added by a later version still reads as a refusal.
    auto frame = coop::encodeFrame(newerServer);
    frame[1] = static_cast<char>(200);
    const auto unknown = coop::decodeRefused(frame);
    REQUIRE(unknown.has_value());
    CHECK(unknown->reason == coop::RefusalReason::malformed);
}
