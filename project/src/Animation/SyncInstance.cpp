#include "SyncInstance.h"
#include "Graph.h"

namespace Animation
{
	bool SyncInstance::Synchronize(Graph* a_grph, const std::function<void(Graph*, bool)>& a_visitFunc)
	{
		auto d = data.lock();
		std::shared_ptr<Graph> owner = d->owner.lock();
		if (!owner)
			return false;

		for (auto& g : d->updatedGraphs) {
			if (g == a_grph) {
				d->ownerUpdatedThisFrame = false;
				d->updatedGraphs.clear();
				break;
			}
		}
		d->updatedGraphs.push_back(a_grph);

		if (a_grph == owner.get()) {
			d->ownerUpdatedThisFrame = true;
		} else {
			bool updated = d->ownerUpdatedThisFrame;
			d.unlock();
			std::unique_lock l{ owner->lock };
			a_visitFunc(owner.get(), updated);
		}
		return true;
	}

	void SyncInstance::ExchangeOwner(const std::function<Graph*(Graph*)>& a_exchangeFunc)
	{
		auto d = data.lock();
		std::shared_ptr<Graph> currentOwner = d->owner.lock();
		Graph* newOwner = a_exchangeFunc(currentOwner.get());

		if (newOwner != currentOwner.get()) {
			if (newOwner) {
				d->owner = std::static_pointer_cast<Graph>(newOwner->shared_from_this());
			} else {
				d->owner.reset();
			}
		}
	}

	void SyncInstance::AddMember(Graph* a_grph)
	{
		data.lock()->memberHandles.emplace(a_grph, std::static_pointer_cast<Graph>(a_grph->shared_from_this()));
	}

	void SyncInstance::RemoveMember(Graph* a_grph)
	{
		data.lock()->memberHandles.erase(a_grph);
	}
}