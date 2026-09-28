#include <TiltedOnlinePCH.h>

#include <Systems/InterpolationSystem.h>
#include <Components.h>

#include <iterator>

#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>

#include <Games/References.h>
#include <World.h>

namespace
{
// Hard bound on the sample window. The tick-driven trim below normally leaves
// three or four samples; this only matters when the play head does not advance,
// which is what a clock resync looks like from here.
constexpr size_t kMaxSamples = 8;

// The play head can end up past the newest sample when a packet is late. The
// old code clamped the segment parameter to 1.0, which pins the actor to the
// last known position and then snaps it forward the moment the packet lands.
// Letting the parameter run a little past the segment instead follows the last
// known velocity across the gap: a short overshoot is far less visible than a
// freeze followed by a jump.
constexpr float kMaxExtrapolation = 1.25f;

// Uniform Catmull-Rom through p1..p2, with p0 and p3 supplying the tangents.
// Linear interpolation gives C0 continuity only, so every snapshot boundary is
// a step change in direction; this smooths those joints out.
glm::vec3 CatmullRom(const glm::vec3& aP0, const glm::vec3& aP1, const glm::vec3& aP2, const glm::vec3& aP3, const float aT) noexcept
{
    const float t2 = aT * aT;
    const float t3 = t2 * aT;

    return 0.5f * (2.f * aP1 + (aP2 - aP0) * aT + (2.f * aP0 - 5.f * aP1 + 4.f * aP2 - aP3) * t2 + (-aP0 + 3.f * aP1 - 3.f * aP2 + aP3) * t3);
}
} // namespace

void InterpolationSystem::Update(Actor* apActor, InterpolationComponent& aInterpolationComponent, const uint64_t aTick) noexcept
{
    auto& movements = aInterpolationComponent.TimePoints;

    if (movements.size() < 2)
        return;

    // Keep the window bounded. Newest samples win; dropping from the front is
    // what makes this safe against a play head that has stopped advancing.
    while (movements.size() > kMaxSamples)
        movements.pop_front();

    // Advance the window to the active segment, but stop one sample short of
    // it: that sample is the cubic's p0, and without it every segment would
    // start with a tangent of zero. p3, the sample after the segment, is what
    // the window must never give up, hence the size > 3 guard.
    while (movements.size() > 3)
    {
        const auto third = std::next(std::next(movements.begin()));
        if (third->Tick > aTick)
            break;

        movements.pop_front();
    }

    // Walk to the segment that brackets aTick. When the play head has run past
    // every sample the last pair is kept, so delta comes out above 1 and the
    // curve extrapolates along the final segment instead of freezing.
    auto itFirst = movements.begin();
    auto itSecond = std::next(itFirst);

    while (std::next(itSecond) != movements.end() && itSecond->Tick <= aTick)
    {
        ++itFirst;
        ++itSecond;
    }

    const auto& first = *itFirst;
    const auto& second = *itSecond;

    // Clamping the outer samples to the segment ends degrades the curve to the
    // linear case when the window does not hold them, rather than overshooting.
    const auto& previous = itFirst == movements.begin() ? first : *std::prev(itFirst);
    const auto& following = std::next(itSecond) == movements.end() ? second : *std::next(itSecond);

    auto delta = 0.f;
    const auto tickDelta = static_cast<float>(second.Tick - first.Tick);
    if (tickDelta > 0.f)
        delta = static_cast<float>(aTick - first.Tick) / tickDelta;

    // Guard both ends. A play head behind the window start would extrapolate
    // backwards and yank the actor away from its first known sample; a play
    // head past the window end follows the last segment, bounded above.
    if (delta < 0.f)
        delta = 0.f;
    else
        delta = TiltedPhoques::Min(delta, kMaxExtrapolation);

    const NiPoint3 position{CatmullRom(previous.Position, first.Position, second.Position, following.Position, delta)};

    aInterpolationComponent.Position = position;

    // Don't try to move a null actor
    if (!apActor)
        return;

    apActor->ForcePosition(position);
    apActor->LoadAnimationVariables(second.Variables);

    if (apActor->currentProcess && apActor->currentProcess->middleProcess)
    {
        apActor->currentProcess->middleProcess->direction = second.Direction;
    }

    auto rotA = first.Rotation;
    auto rotB = second.Rotation;

    const auto deltaX = TiltedPhoques::DeltaAngle(rotA.x, rotB.x, true) * delta;
    const auto deltaY = TiltedPhoques::DeltaAngle(rotA.y, rotB.y, true) * delta;
    const auto deltaZ = TiltedPhoques::DeltaAngle(rotA.z, rotB.z, true) * delta;

    auto finalX = TiltedPhoques::Mod(rotA.x + deltaX, float(TiltedPhoques::Pi * 2));
    if (finalX > 0.f && finalX > float(TiltedPhoques::Pi / 2))
        finalX -= TiltedPhoques::Pi * 2;

    const auto finalY = TiltedPhoques::Mod(rotA.y + deltaY, float(TiltedPhoques::Pi * 2));
    const auto finalZ = TiltedPhoques::Mod(rotA.z + deltaZ, float(TiltedPhoques::Pi * 2));

    apActor->SetRotation(finalX, finalY, finalZ);
}

void InterpolationSystem::AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint) noexcept
{
    auto itor = std::begin(aInterpolationComponent.TimePoints);
    const auto end = std::cend(aInterpolationComponent.TimePoints);

    while (itor != end)
    {
        if (itor->Tick > acPoint.Tick)
        {
            aInterpolationComponent.TimePoints.insert(itor, acPoint);

            return;
        }

        ++itor;
    }

    aInterpolationComponent.TimePoints.push_back(acPoint);
}

InterpolationComponent& InterpolationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    return aWorld.emplace_or_replace<InterpolationComponent>(aEntity);
}

void InterpolationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<InterpolationComponent>(aEntity))
        aWorld.remove<InterpolationComponent>(aEntity);
}
