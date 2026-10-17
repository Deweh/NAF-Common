#include "Manager.h"

namespace Morph
{
	Manager* Manager::GetSingleton()
	{
		static Manager singleton;
		return &singleton;
	}

	std::shared_ptr<ActorMorphs> Manager::Get(uint32_t a_formId, bool a_create)
	{
		{
			std::shared_lock l{ lock };
			if (auto iter = actors.find(a_formId); iter != actors.end())
				return iter->second;
		}
		if (!a_create)
			return nullptr;
		std::unique_lock l{ lock };
		auto& entry = actors[a_formId];
		if (!entry)
			entry = std::make_shared<ActorMorphs>();
		return entry;
	}

	void Manager::Remove(uint32_t a_formId)
	{
		std::unique_lock l{ lock };
		actors.erase(a_formId);
	}

	void Manager::ForEach(const std::function<void(uint32_t, const std::shared_ptr<ActorMorphs>&)>& a_func) const
	{
		std::vector<std::pair<uint32_t, std::shared_ptr<ActorMorphs>>> copy;
		{
			std::shared_lock l{ lock };
			copy.assign(actors.begin(), actors.end());
		}
		for (auto& [id, morphs] : copy) {
			a_func(id, morphs);
		}
	}

	void Manager::Reload()
	{
		drivers.Load();
		triFiles.Clear();
		dataGeneration.fetch_add(1, std::memory_order_release);
	}

	void UpdateActorsWithoutGraphs()
	{
		// See the declaration: correctives for actors without a NAF graph aren't implemented yet.
	}
}
