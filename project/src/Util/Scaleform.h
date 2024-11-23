#pragma once

namespace Util::Scaleform
{
	template <class... Ts>
	inline RE::Scaleform::GFx::Value InvokeAS3(RE::Scaleform::GFx::Value& obj, const std::string_view& funcName, Ts&&... inputs)
	{
		RE::Scaleform::GFx::Value result;
		if (!obj.IsObject()) {
			return result;
		}

		std::array<RE::Scaleform::GFx::Value, sizeof...(inputs)> args;
		size_t i = 0;

		([&] {
			args[i++] = inputs;
		}(),...);

		obj.Invoke(funcName.data(), &result, args.data(), sizeof...(inputs));

		return result;
	}

	inline RE::Scaleform::GFx::Value InvokeAS3(RE::Scaleform::GFx::Value& obj, const std::string_view& funcName)
	{
		RE::Scaleform::GFx::Value result;
		if (!obj.IsObject()) {
			return result;
		}

		obj.Invoke(funcName.data(), &result, nullptr, 0);
		return result;
	}
}