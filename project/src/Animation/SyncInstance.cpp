#include "SyncInstance.h"
#include "Graph.h"

namespace Animation
{
	void SyncInstance::InstData::GraphRef::reset()
	{
		ptr = nullptr;
		handle.reset();
	}

	std::shared_ptr<Graph> SyncInstance::InstData::GraphRef::lock() const
	{
		return handle.lock();
	}

	bool SyncInstance::InstData::GraphRef::operator==(const Graph* a_rhs) const
	{
		return ptr == a_rhs;
	}

	void SyncInstance::InstData::GraphRef::operator=(Graph* a_rhs)
	{
		ptr = a_rhs;
		handle = std::static_pointer_cast<Graph>(a_rhs->shared_from_this());
	}

	bool SyncInstance::Synchronize(Graph* a_grph, const std::function<void(Graph*, bool)>& a_visitFunc)
	{
		std::shared_ptr<Graph> owner;
		auto d = data.lock();
		owner = d->owner.lock();
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

	void SyncInstance::AddMember(Graph* a_grph, bool a_addAsOwner)
	{
		auto d = data.lock();
		auto iter = std::find(d->members.begin(), d->members.end(), a_grph);
		if (iter == d->members.end()) {
			auto& newMember = d->members.emplace_back();
			newMember = a_grph;
		}
		if (a_addAsOwner) {
			d->owner = a_grph;
		}
	}

	void SyncInstance::RemoveMember(Graph* a_grph)
	{
		auto d = data.lock();
		auto iter = std::find(d->members.begin(), d->members.end(), a_grph);
		if (iter != d->members.end()) {
			d->members.erase(iter);
		}
		if (d->owner == a_grph) {
			d->owner.reset();
		}
	}
}