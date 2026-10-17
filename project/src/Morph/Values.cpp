#include "Values.h"

namespace Morph
{
	namespace
	{
		// Changes smaller than this don't count as a change, so a value settling doesn't cause uploads every frame.
		constexpr float kEpsilon = 1e-4f;
	}

	std::string ToLowerName(std::string_view a_name)
	{
		std::string result{ a_name };
		std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	std::string_view GetLayerName(Layer a_layer)
	{
		switch (a_layer) {
		case Layer::kStatic:
			return "static";
		case Layer::kAnimation:
			return "animation";
		case Layer::kCorrective:
			return "corrective";
		default:
			return "?";
		}
	}

	void ActorMorphs::Set(Layer a_layer, std::string_view a_owner, std::string_view a_morph, float a_value)
	{
		std::scoped_lock l{ lock };
		auto& layer = layers[static_cast<size_t>(a_layer)];
		const std::string name = ToLowerName(a_morph);
		auto morph = layer.find(name);
		if (a_value == 0.0f) {
			if (morph == layer.end())
				return;
			auto owner = morph->second.find(a_owner);
			if (owner == morph->second.end())
				return;
			const bool changed = std::abs(owner->second) > kEpsilon;
			morph->second.erase(owner);
			if (morph->second.empty())
				layer.erase(morph);
			if (changed)
				MarkChanged();
			return;
		}
		if (morph == layer.end())
			morph = layer.emplace(name, std::map<std::string, float, std::less<>>{}).first;
		auto [owner, added] = morph->second.try_emplace(std::string{ a_owner }, a_value);
		if (added || std::abs(owner->second - a_value) > kEpsilon) {
			owner->second = a_value;
			MarkChanged();
		} else {
			owner->second = a_value;
		}
	}

	void ActorMorphs::SetAll(Layer a_layer, std::string_view a_owner, const Values& a_values)
	{
		std::scoped_lock l{ lock };
		auto& layer = layers[static_cast<size_t>(a_layer)];
		bool changed = false;
		// Drop the owner's values that aren't in a_values.
		for (auto morph = layer.begin(); morph != layer.end();) {
			auto owner = morph->second.find(a_owner);
			const bool keep = owner == morph->second.end() || std::ranges::any_of(a_values, [&](const auto& a_value) { return a_value.second != 0.0f && a_value.first == morph->first; });
			if (!keep) {
				changed |= std::abs(owner->second) > kEpsilon;
				morph->second.erase(owner);
			}
			morph = morph->second.empty() ? layer.erase(morph) : std::next(morph);
		}
		for (const auto& [name, value] : a_values) {
			if (value == 0.0f)
				continue;
			auto& owners = layer[name];
			auto [owner, added] = owners.try_emplace(std::string{ a_owner }, value);
			if (added || std::abs(owner->second - value) > kEpsilon) {
				changed = true;
			}
			owner->second = value;
		}
		if (changed)
			MarkChanged();
	}

	void ActorMorphs::Clear(Layer a_layer)
	{
		std::scoped_lock l{ lock };
		auto& layer = layers[static_cast<size_t>(a_layer)];
		if (!layer.empty()) {
			layer.clear();
			MarkChanged();
		}
	}

	void ActorMorphs::ClearAll()
	{
		for (size_t i = 0; i < layers.size(); i++) {
			Clear(static_cast<Layer>(i));
		}
	}

	Values ActorMorphs::GetTotals() const
	{
		std::map<std::string, float, std::less<>> totals;
		{
			std::scoped_lock l{ lock };
			for (const auto& layer : layers) {
				for (const auto& [name, owners] : layer) {
					for (const auto& [owner, value] : owners) {
						totals[name] += value;
					}
				}
			}
		}
		Values result;
		result.reserve(totals.size());
		for (auto& [name, value] : totals) {
			if (std::abs(value) > kEpsilon)
				result.emplace_back(name, value);
		}
		return result;
	}

	void ActorMorphs::ForEach(const std::function<void(Layer, std::string_view, std::string_view, float)>& a_func) const
	{
		std::scoped_lock l{ lock };
		for (size_t i = 0; i < layers.size(); i++) {
			for (const auto& [name, owners] : layers[i]) {
				for (const auto& [owner, value] : owners) {
					a_func(static_cast<Layer>(i), owner, name, value);
				}
			}
		}
	}

	void ActorMorphs::MarkChanged()
	{
		version.fetch_add(1, std::memory_order_release);
	}
}
