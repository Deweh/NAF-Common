#pragma once
#include "Animation/Studio/SceneObjects.h"

namespace Animation::Studio
{
	// The project's scene objects in outliner order, with an id index so lookups don't scan.
	class ObjectStore
	{
	public:
		using Container = std::vector<std::unique_ptr<SceneObject>>;

		[[nodiscard]] size_t size() const { return m_objects.size(); }
		[[nodiscard]] bool empty() const { return m_objects.empty(); }
		// Iteration can't move objects out: that goes through Extract, which keeps the index in step.
		[[nodiscard]] Container::const_iterator begin() const { return m_objects.begin(); }
		[[nodiscard]] Container::const_iterator end() const { return m_objects.end(); }

		[[nodiscard]] SceneObject* Find(ObjectId a_id);
		[[nodiscard]] const SceneObject* Find(ObjectId a_id) const;
		template <class T>
		[[nodiscard]] T* Find(ObjectId a_id)
		{
			return ObjectCast<T>(Find(a_id));
		}
		template <class T>
		[[nodiscard]] const T* Find(ObjectId a_id) const
		{
			return ObjectCast<T>(Find(a_id));
		}
		[[nodiscard]] ModifierObject* FindModifier(ObjectId a_id) { const auto object = Find(a_id); return object ? object->AsModifier() : nullptr; }
		[[nodiscard]] const ModifierObject* FindModifier(ObjectId a_id) const { const auto object = Find(a_id); return object ? object->AsModifier() : nullptr; }
		// The actor object bound to a game actor.
		[[nodiscard]] ActorObject* FindActor(const RE::Actor* a_actor) const;

		template <class T, class F>
		void ForEach(F&& a_func) const
		{
			for (const auto& object : m_objects) {
				if (const auto typed = ObjectCast<T>(object.get())) a_func(*typed);
			}
		}

		// a_index is clamped to the end.
		void Insert(size_t a_index, std::unique_ptr<SceneObject> a_object);
		void PushBack(std::unique_ptr<SceneObject> a_object);
		// Removes the object and returns it, with its position. Null if it isn't in the store.
		std::unique_ptr<SceneObject> Extract(ObjectId a_id, size_t& a_index);

		// The modifier owning a locator (an IK chain's effector or pole, or a spline's control), or kNoObject.
		[[nodiscard]] ObjectId GetLocatorOwner(ObjectId a_locator) const;
		// Scene space, an actor (root or one of its bones), or a locator.
		[[nodiscard]] bool IsValidParent(const ParentRef& a_parent) const;
		// Whether parenting a_locator to a_parent would make it its own ancestor.
		[[nodiscard]] bool WouldCreateLocatorLoop(ObjectId a_locator, const ParentRef& a_parent) const;

	private:
		Container m_objects;
		std::unordered_map<ObjectId, SceneObject*> m_index;
	};
}
