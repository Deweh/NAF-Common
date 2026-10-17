#pragma once

// Real-time body morphs: the values. Each actor has a set of named morph values (BodySlide/LooksMenu TRI morph names),
// kept in layers that are added together. Morph::Manager (Manager.h) owns every actor's values; the game frontend reads
// the totals and applies them to the actor's meshes (Backend.h). Names are case-insensitive and stored lowercase.
namespace Morph
{
	enum class Layer : uint8_t
	{
		kStatic,      // Set by plugins and console commands, keyed by the caller.
		kAnimation,   // Morph tracks of the playing animation.
		kCorrective,  // Pose-space correctives (Morph/Drivers.h).

		kTotal
	};

	using Values = std::vector<std::pair<std::string, float>>;

	class ActorMorphs
	{
	public:
		// a_owner keys the value within its layer, so several callers can set the same morph; their values add up.
		// A value of 0 removes the entry.
		void Set(Layer a_layer, std::string_view a_owner, std::string_view a_morph, float a_value);
		// Replaces every value a_owner has in a_layer.
		void SetAll(Layer a_layer, std::string_view a_owner, const Values& a_values);
		void Clear(Layer a_layer);
		void ClearAll();

		// Changes when the totals change by more than a negligible amount.
		[[nodiscard]] uint64_t GetVersion() const { return version.load(std::memory_order_acquire); }
		// The non-zero totals, sorted by name.
		[[nodiscard]] Values GetTotals() const;
		// Every value: layer, owner, morph, value. For the console and UI.
		void ForEach(const std::function<void(Layer, std::string_view, std::string_view, float)>& a_func) const;

	private:
		// morph -> owner -> value
		using LayerMap = std::map<std::string, std::map<std::string, float, std::less<>>, std::less<>>;

		void MarkChanged();

		mutable std::mutex lock;
		std::array<LayerMap, static_cast<size_t>(Layer::kTotal)> layers;
		std::atomic<uint64_t> version{ 0 };
	};

	[[nodiscard]] std::string ToLowerName(std::string_view a_name);
	[[nodiscard]] std::string_view GetLayerName(Layer a_layer);
}
