#include "SimulatedNetwork.h"

#include <map>
#include <vector>

namespace daw::test
{

namespace
{

struct Wire
{
    SimulatedNetwork::Conditions conditions;
    bool open = true;
    std::vector<juce::MemoryBlock> inbox[2];
    juce::String problem[2];
};

} // namespace

struct SimulatedNetwork::Impl
{
    explicit Impl(juce::int64 seed)
        : random(seed)
    {
    }

    struct Scheduled
    {
        std::shared_ptr<Wire> wire;
        int toEnd = 0;
        juce::MemoryBlock frame;
    };

    void send(const std::shared_ptr<Wire>& wire, int fromEnd, const juce::MemoryBlock& frame)
    {
        if (!wire->open)
            return;
        ++framesSent;
        bytesSent += frame.getSize();
        const auto& conditions = wire->conditions;
        if (random.nextDouble() < conditions.loss)
            return;
        const int copies = random.nextDouble() < conditions.duplication ? 2 : 1;
        for (int copy = 0; copy < copies; ++copy)
        {
            const auto delay = conditions.minimumDelay
                               + random.nextDouble()
                                     * (conditions.maximumDelay - conditions.minimumDelay);
            scheduled.emplace(std::make_pair(time + delay, order++),
                              Scheduled{wire, 1 - fromEnd, frame});
        }
    }

    void advance(double seconds)
    {
        time += seconds;
        while (!scheduled.empty() && scheduled.begin()->first.first <= time)
        {
            auto& item = scheduled.begin()->second;
            if (item.wire->open)
                item.wire->inbox[item.toEnd].push_back(std::move(item.frame));
            scheduled.erase(scheduled.begin());
        }
    }

    juce::Random random;
    double time = 0.0;
    std::uint64_t order = 0;
    std::multimap<std::pair<double, std::uint64_t>, Scheduled> scheduled;
    std::vector<std::weak_ptr<Wire>> wires;
    std::uint64_t framesSent = 0;
    std::uint64_t bytesSent = 0;
};

namespace
{

class SimulatedLink final : public net::Link
{
public:
    SimulatedLink(std::shared_ptr<SimulatedNetwork::Impl> network, std::shared_ptr<Wire> sharedWire,
                  int whichEnd)
        : impl(std::move(network)), wire(std::move(sharedWire)), end(whichEnd)
    {
    }

    ~SimulatedLink() override { close(); }

    void send(juce::MemoryBlock frame) override { impl->send(wire, end, frame); }

    std::vector<juce::MemoryBlock> receive() override
    {
        return std::exchange(wire->inbox[end], {});
    }

    State getState() const override { return wire->open ? State::open : State::closed; }
    juce::String getProblem() const override { return wire->problem[end]; }

    void close() override
    {
        if (!wire->open)
            return;
        wire->open = false;
        wire->problem[1 - end] = "The other end closed the connection.";
    }

private:
    std::shared_ptr<SimulatedNetwork::Impl> impl;
    std::shared_ptr<Wire> wire;
    int end;
};

} // namespace

SimulatedNetwork::SimulatedNetwork(juce::int64 seed)
    : impl(std::make_shared<Impl>(seed))
{
}

SimulatedNetwork::~SimulatedNetwork() = default;

double SimulatedNetwork::now() const noexcept
{
    return impl->time;
}

void SimulatedNetwork::advance(double seconds)
{
    impl->advance(seconds);
}

SimulatedNetwork::Pair SimulatedNetwork::connect(Conditions conditions)
{
    auto wire = std::make_shared<Wire>();
    wire->conditions = conditions;
    impl->wires.push_back(wire);
    return {std::make_unique<SimulatedLink>(impl, wire, 0),
            std::make_unique<SimulatedLink>(impl, wire, 1)};
}

void SimulatedNetwork::cutAll()
{
    for (const auto& weak : impl->wires)
        if (const auto wire = weak.lock(); wire != nullptr && wire->open)
        {
            wire->open = false;
            wire->problem[0] = wire->problem[1] = "The connection broke.";
        }
    impl->wires.clear();
}

std::uint64_t SimulatedNetwork::getFramesSent() const noexcept
{
    return impl->framesSent;
}

std::uint64_t SimulatedNetwork::getBytesSent() const noexcept
{
    return impl->bytesSent;
}

} // namespace daw::test
