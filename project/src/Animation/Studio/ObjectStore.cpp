#include "ObjectStore.h"

#include "Modifiers/Modifier.h"

namespace Animation::Studio
{
	SceneObject* ObjectStore::Find(ObjectId a_id)
	{
		const auto iter = m_index.find(a_id);
		return iter != m_index.end() ? iter->second : nullptr;
	}

	const SceneObject* ObjectStore::Find(ObjectId a_id) const
	{
		const auto iter = m_index.find(a_id);
		return iter != m_index.end() ? iter->second : nullptr;
	}

	ActorObject* ObjectStore::FindActor(const RE::Actor* a_actor) const
	{
		for (const auto& object : m_objects) {
			if (const auto actor = ObjectCast<ActorObject>(object.get()); actor && actor->actor.get() == a_actor) return actor;
		}
		return nullptr;
	}

	void ObjectStore::Insert(size_t a_index, std::unique_ptr<SceneObject> a_object)
	{
		[[maybe_unused]] const bool inserted = m_index.emplace(a_object->id, a_object.get()).second;
		assert(inserted);
		m_objects.insert(m_objects.begin() + (std::min)(a_index, m_objects.size()), std::move(a_object));
	}

	void ObjectStore::PushBack(std::unique_ptr<SceneObject> a_object)
	{
		Insert(m_objects.size(), std::move(a_object));
	}

	std::unique_ptr<SceneObject> ObjectStore::Extract(ObjectId a_id, size_t& a_index)
	{
		const auto iter = std::ranges::find(m_objects, a_id, [](const auto& a_object) { return a_object->id; });
		if (iter == m_objects.end()) return nullptr;
		a_index = static_cast<size_t>(std::distance(m_objects.begin(), iter));
		auto object = std::move(*iter);
		m_objects.erase(iter);
		m_index.erase(a_id);
		return object;
	}

	ObjectId ObjectStore::GetLocatorOwner(ObjectId a_locator) const
	{
		if (a_locator == kNoObject) return kNoObject;
		for (const auto& object : m_objects) {
			if (const auto modifier = object->AsModifier(); modifier && modifier->UsesLocator(a_locator)) return modifier->id;
		}
		return kNoObject;
	}

	bool ObjectStore::IsValidParent(const ParentRef& a_parent) const
	{
		if (a_parent.object == kNoObject) return true;
		if (const auto actor = Find<ActorObject>(a_parent.object)) {
			return a_parent.bone >= -1 && a_parent.bone < static_cast<int32_t>(actor->bones.size());
		}
		return a_parent.bone == -1 && Find<LocatorObject>(a_parent.object) != nullptr;
	}

	bool ObjectStore::WouldCreateLocatorLoop(ObjectId a_locator, const ParentRef& a_parent) const
	{
		auto parent = a_parent;
		for (size_t guard = 0; guard <= m_objects.size(); ++guard) {
			if (parent.object == a_locator) return true;
			const auto locator = Find<LocatorObject>(parent.object);
			if (!locator) return false;
			parent = locator->parent;
		}
		return true;
	}
}
